/* slotted.c: Variable-length slotted-page record management subsystem.
   Textbook dual-direction slotted page layout:
   - Header & slot directory grow FORWARD from offset 0
   - Record data payloads grow BACKWARD from PF_PAGE_SIZE (4092)
   - On-demand in-place compaction defragments dead space from deleted records
   - 4-byte packed RIDs compatible with AM layer recId
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "slotted.h"

static void SP_InitPage(char *pageBuf) {
    SPHeader *hdr = (SPHeader *)pageBuf;
    hdr->numSlots = 0;
    hdr->freeSpaceOffset = PF_PAGE_SIZE;
    hdr->freeSpaceBytes = PF_PAGE_SIZE - sizeof(SPHeader);
    hdr->numRecords = 0;
}

int SP_CreateFile(char *fname) {
    return PF_CreateFile(fname);
}

int SP_DestroyFile(char *fname) {
    return PF_DestroyFile(fname);
}

int SP_OpenFile(char *fname) {
    return PF_OpenFile(fname);
}

int SP_CloseFile(int spFd) {
    return PF_CloseFile(spFd);
}

int SP_CompactPage(char *pageBuf) {
    SPHeader *hdr = (SPHeader *)pageBuf;
    SPSlot *slots = (SPSlot *)(pageBuf + sizeof(SPHeader));
    char tempBuf[PF_PAGE_SIZE];
    short newOffset = PF_PAGE_SIZE;
    int i;

    /* Copy active records into temp buffer growing downward from PF_PAGE_SIZE */
    for (i = 0; i < hdr->numSlots; i++) {
        if (slots[i].length > 0) {
            newOffset -= slots[i].length;
            memcpy(tempBuf + newOffset, pageBuf + slots[i].offset, slots[i].length);
            slots[i].offset = newOffset; /* update directory offset */
        }
    }

    /* Copy compacted record data area back into pageBuf */
    if (newOffset < PF_PAGE_SIZE) {
        int compactedSize = PF_PAGE_SIZE - newOffset;
        memcpy(pageBuf + newOffset, tempBuf + newOffset, compactedSize);
    }

    hdr->freeSpaceOffset = newOffset;
    hdr->freeSpaceBytes = newOffset - (sizeof(SPHeader) + hdr->numSlots * sizeof(SPSlot));
    return PFE_OK;
}

int SP_InsertRecord(int spFd, char *recData, int recLen, RID *rid) {
    char *pageBuf = NULL;
    SPHeader *hdr = NULL;
    SPSlot *slots = NULL;
    int targetPage = -1;
    int targetSlot = -1;
    int neededBytes = 0;
    int contiguousFree = 0;
    int numPages;
    int i;
    int error;

    if (recLen <= 0 || recLen > (int)(PF_PAGE_SIZE - sizeof(SPHeader) - sizeof(SPSlot))) {
        return -1; /* Record too large to fit in single page */
    }

    /* 1. Fast O(1) fetch of the last allocated page in the file */
    numPages = PF_GetNumPages(spFd);
    if (numPages > 0) {
        targetPage = numPages - 1;
        error = PF_GetThisPage(spFd, targetPage, &pageBuf);
        if (error == PFE_OK) {
            hdr = (SPHeader *)pageBuf;
            slots = (SPSlot *)(pageBuf + sizeof(SPHeader));

            /* Check for existing tombstone slot to reuse */
            targetSlot = -1;
            for (i = 0; i < hdr->numSlots; i++) {
                if (slots[i].length == -1) {
                    targetSlot = i;
                    break;
                }
            }

            if (targetSlot < 0) {
                targetSlot = hdr->numSlots;
                neededBytes = recLen + sizeof(SPSlot);
            } else {
                neededBytes = recLen; /* slot already present in directory */
            }

            contiguousFree = hdr->freeSpaceOffset - (sizeof(SPHeader) +
                             ((targetSlot == hdr->numSlots ? hdr->numSlots + 1 : hdr->numSlots) * sizeof(SPSlot)));

            /* If page has enough total free bytes but space is fragmented, compact in-place */
            if (neededBytes <= hdr->freeSpaceBytes && neededBytes > contiguousFree) {
                SP_CompactPage(pageBuf);
                contiguousFree = hdr->freeSpaceOffset - (sizeof(SPHeader) +
                                 ((targetSlot == hdr->numSlots ? hdr->numSlots + 1 : hdr->numSlots) * sizeof(SPSlot)));
            }

            /* If it still doesn't fit in this page, unfix and prepare to allocate a new page */
            if (neededBytes > hdr->freeSpaceBytes || neededBytes > contiguousFree) {
                PF_UnfixPage(spFd, targetPage, FALSE);
                targetPage = -1;
            }
        } else {
            targetPage = -1;
        }
    }

    /* 2. Allocate a new page if no existing page had room */
    if (targetPage < 0) {
        error = PF_AllocPage(spFd, &targetPage, &pageBuf);
        if (error != PFE_OK) return error;

        SP_InitPage(pageBuf);
        hdr = (SPHeader *)pageBuf;
        slots = (SPSlot *)(pageBuf + sizeof(SPHeader));
        targetSlot = 0;
        neededBytes = recLen + sizeof(SPSlot);
    }

    /* 3. Insert record into targetPage at targetSlot */
    hdr->freeSpaceOffset -= recLen;
    memcpy(pageBuf + hdr->freeSpaceOffset, recData, recLen);

    if (targetSlot == hdr->numSlots) {
        hdr->numSlots++;
    }
    slots[targetSlot].offset = hdr->freeSpaceOffset;
    slots[targetSlot].length = recLen;

    hdr->freeSpaceBytes = hdr->freeSpaceOffset - (sizeof(SPHeader) + hdr->numSlots * sizeof(SPSlot));
    hdr->numRecords++;

    PF_MarkDirty(spFd, targetPage);
    PF_UnfixPage(spFd, targetPage, TRUE);

    rid->pageNum = targetPage;
    rid->slotNum = (short)targetSlot;
    return PFE_OK;
}

int SP_DeleteRecord(int spFd, RID rid) {
    char *pageBuf;
    SPHeader *hdr;
    SPSlot *slots;
    int error;

    error = PF_GetThisPage(spFd, rid.pageNum, &pageBuf);
    if (error != PFE_OK) return error;

    hdr = (SPHeader *)pageBuf;
    slots = (SPSlot *)(pageBuf + sizeof(SPHeader));

    if (rid.slotNum < 0 || rid.slotNum >= hdr->numSlots) {
        PF_UnfixPage(spFd, rid.pageNum, FALSE);
        return -1; /* Invalid slot number */
    }

    if (slots[rid.slotNum].length <= 0) {
        PF_UnfixPage(spFd, rid.pageNum, FALSE);
        return -2; /* Record already deleted */
    }

    /* Reclaim space & mark tombstone */
    slots[rid.slotNum].length = -1;
    hdr->numRecords--;
    hdr->freeSpaceBytes = hdr->freeSpaceOffset - (sizeof(SPHeader) + hdr->numSlots * sizeof(SPSlot));

    PF_MarkDirty(spFd, rid.pageNum);
    PF_UnfixPage(spFd, rid.pageNum, TRUE);
    return PFE_OK;
}

int SP_GetRecord(int spFd, RID rid, char *buf, int *recLen) {
    char *pageBuf;
    SPHeader *hdr;
    SPSlot *slots;
    int error;

    error = PF_GetThisPage(spFd, rid.pageNum, &pageBuf);
    if (error != PFE_OK) return error;

    hdr = (SPHeader *)pageBuf;
    slots = (SPSlot *)(pageBuf + sizeof(SPHeader));

    if (rid.slotNum < 0 || rid.slotNum >= hdr->numSlots || slots[rid.slotNum].length <= 0) {
        PF_UnfixPage(spFd, rid.pageNum, FALSE);
        return -1;
    }

    *recLen = slots[rid.slotNum].length;
    memcpy(buf, pageBuf + slots[rid.slotNum].offset, *recLen);
    buf[*recLen] = '\0';

    PF_UnfixPage(spFd, rid.pageNum, FALSE);
    return PFE_OK;
}

int SP_OpenScan(int spFd, SPScan *scan) {
    scan->spFd = spFd;
    scan->currPage = -1;
    scan->currSlot = -1;
    return PFE_OK;
}

int SP_GetNextRecord(SPScan *scan, RID *rid, char *buf, int *recLen) {
    char *pageBuf = NULL;
    int pageNum = scan->currPage;
    int error;

    while (1) {
        if (pageNum < 0 || scan->currSlot < 0) {
            if (pageNum < 0) {
                error = PF_GetFirstPage(scan->spFd, &pageNum, &pageBuf);
            } else {
                error = PF_GetNextPage(scan->spFd, &pageNum, &pageBuf);
            }

            if (error != PFE_OK) {
                scan->currPage = -1;
                return error; /* EOF or error */
            }
            scan->currPage = pageNum;
            scan->currSlot = 0;
        } else {
            error = PF_GetThisPage(scan->spFd, pageNum, &pageBuf);
            if (error != PFE_OK) return error;
        }

        {
            SPHeader *hdr = (SPHeader *)pageBuf;
            SPSlot *slots = (SPSlot *)(pageBuf + sizeof(SPHeader));

            while (scan->currSlot < hdr->numSlots) {
                int s = scan->currSlot++;
                if (slots[s].length > 0) {
                    *recLen = slots[s].length;
                    memcpy(buf, pageBuf + slots[s].offset, *recLen);
                    buf[*recLen] = '\0';
                    rid->pageNum = pageNum;
                    rid->slotNum = (short)s;

                    PF_UnfixPage(scan->spFd, pageNum, FALSE);
                    return PFE_OK;
                }
            }

            /* Finished all slots on this page; advance to next page */
            PF_UnfixPage(scan->spFd, pageNum, FALSE);
            scan->currSlot = -1;
        }
    }
}

int SP_CloseScan(SPScan *scan) {
    scan->currPage = -1;
    scan->currSlot = -1;
    return PFE_OK;
}

int SP_GetFileStats(int spFd, SPFileStats *stats) {
    int pageNum = -1;
    char *pageBuf;
    int i;
    int error;

    memset(stats, 0, sizeof(SPFileStats));

    error = PF_GetFirstPage(spFd, &pageNum, &pageBuf);
    while (error == PFE_OK) {
        SPHeader *hdr = (SPHeader *)pageBuf;
        SPSlot *slots = (SPSlot *)(pageBuf + sizeof(SPHeader));

        stats->totalPages++;
        stats->totalAllocatedBytes += PF_PAGE_SIZE;

        for (i = 0; i < hdr->numSlots; i++) {
            if (slots[i].length > 0) {
                stats->totalRecords++;
                stats->totalPayloadBytes += slots[i].length;
            }
        }

        PF_UnfixPage(spFd, pageNum, FALSE);
        error = PF_GetNextPage(spFd, &pageNum, &pageBuf);
    }

    if (stats->totalAllocatedBytes > 0) {
        stats->spaceUtilization = (double)stats->totalPayloadBytes * 100.0 /
                                  (double)stats->totalAllocatedBytes;
    }
    return PFE_OK;
}
