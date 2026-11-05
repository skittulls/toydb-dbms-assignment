/* ambulk.c: True bottom-up B+ tree bulk loading engine.
   - Preserves Page 0 root reservation matching ToyDB traversal invariants
   - Packs leaf nodes to configurable target fill factor
   - Guarantees duplicate key atomicity on leaf pages with overflow guard
   - Propagates subtree minimum keys to parent internal nodes at Level 2+
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ambulk.h"

static char g_bulkAttrType = 'c';
static int g_bulkAttrLength = 4;

static int bulk_entry_compare(const void *a, const void *b) {
    const BulkEntry *ea = (const BulkEntry *)a;
    const BulkEntry *eb = (const BulkEntry *)b;
    int cmp = 0;
    if (g_bulkAttrType == 'i') {
        int ia, ib;
        memcpy(&ia, ea->key, sizeof(int));
        memcpy(&ib, eb->key, sizeof(int));
        if (ia < ib) cmp = -1;
        else if (ia > ib) cmp = 1;
        else cmp = 0;
    } else if (g_bulkAttrType == 'f') {
        float fa, fb;
        memcpy(&fa, ea->key, sizeof(float));
        memcpy(&fb, eb->key, sizeof(float));
        if (fa < fb) cmp = -1;
        else if (fa > fb) cmp = 1;
        else cmp = 0;
    } else {
        cmp = strncmp((const char *)ea->key, (const char *)eb->key, g_bulkAttrLength);
    }
    if (cmp != 0) return cmp;
    if (ea->recId < eb->recId) return -1;
    if (ea->recId > eb->recId) return 1;
    return 0;
}

int AM_BulkLoad(char *fileName, int indexNo, char attrType, int attrLength,
                BulkEntry *entries, int numEntries, int fillFactorPercent) {
    char indexfName[AM_MAX_FNAME_LENGTH];
    int indexFd;
    int reservedRootPageNum;
    char *reservedRootBuf;
    int maxLeafKeys, targetLeafKeys;
    int maxIntKeys;
    int errVal;
    int i;

    BulkPageInfo *currLevelPages = NULL;
    int currLevelCount = 0;
    BulkPageInfo *nextLevelPages = NULL;
    int nextLevelCount = 0;

    if (attrType != 'c' && attrType != 'i' && attrType != 'f') {
        AM_Errno = AME_INVALIDATTRTYPE;
        return AME_INVALIDATTRTYPE;
    }
    if (attrLength < 1 || attrLength > 255) {
        AM_Errno = AME_INVALIDATTRLENGTH;
        return AME_INVALIDATTRLENGTH;
    }
    if (attrLength != 4 && attrType != 'c') {
        AM_Errno = AME_INVALIDATTRLENGTH;
        return AME_INVALIDATTRLENGTH;
    }

    g_bulkAttrType = attrType;
    g_bulkAttrLength = attrLength;

    /* 1. Sort entries in-memory by key ascending */
    qsort(entries, numEntries, sizeof(BulkEntry), bulk_entry_compare);

    /* 2. Create and open index file */
    sprintf(indexfName, "%s.%d", fileName, indexNo);
    PF_DestroyFile(indexfName);
    errVal = PF_CreateFile(indexfName);
    AM_Check;

    indexFd = PF_OpenFile(indexfName);
    if (indexFd < 0) {
        AM_Errno = AME_PF;
        return AME_PF;
    }

    /* 3. Reserve Page 0 upfront for the B+ tree root */
    errVal = PF_AllocPage(indexFd, &reservedRootPageNum, &reservedRootBuf);
    AM_Check;
    PF_UnfixPage(indexFd, reservedRootPageNum, FALSE);

    /* Compute capacities */
    maxLeafKeys = (PF_PAGE_SIZE - AM_sl) / (attrLength + sizeof(int) + 2 * sizeof(short));
    if ((maxLeafKeys % 2) != 0) maxLeafKeys--;
    targetLeafKeys = (maxLeafKeys * fillFactorPercent) / 100;
    if (targetLeafKeys < 1) targetLeafKeys = 1;

    maxIntKeys = (PF_PAGE_SIZE - AM_sint - sizeof(int)) / (attrLength + sizeof(int));
    if ((maxIntKeys % 2) != 0) maxIntKeys--;

    /* 4. Build Leaf Layer (Level 0) */
    currLevelPages = (BulkPageInfo *)malloc(sizeof(BulkPageInfo) * (numEntries + 10));
    currLevelCount = 0;

    if (numEntries == 0) {
        /* Empty index: initialize Page 0 as empty leaf and return */
        AM_LEAFHEADER head;
        errVal = PF_GetThisPage(indexFd, 0, &reservedRootBuf);
        AM_Check;
        head.pageType = 'l';
        head.nextLeafPage = AM_NULL_PAGE;
        head.recIdPtr = PF_PAGE_SIZE;
        head.keyPtr = AM_sl;
        head.freeListPtr = 0;
        head.numinfreeList = 0;
        head.attrLength = attrLength;
        head.numKeys = 0;
        head.maxKeys = maxLeafKeys;
        memcpy(reservedRootBuf, &head, AM_sl);
        PF_MarkDirty(indexFd, 0);
        PF_UnfixPage(indexFd, 0, TRUE);
        PF_CloseFile(indexFd);
        AM_RootPageNum = 0;
        AM_LeftPageNum = 0;
        free(currLevelPages);
        return AME_OK;
    }

    {
        int prevLeafPageNum = -1;
        int currLeafPageNum = -1;
        char *currLeafBuf = NULL;
        AM_LEAFHEADER head;
        int recSize = attrLength + sizeof(short);
        int recNodeSize = sizeof(int) + sizeof(short);

        for (i = 0; i < numEntries; i++) {
            int is_dup = 0;
            if (currLeafPageNum >= 0 && head.numKeys > 0) {
                short prevKeyOff = head.keyPtr - recSize;
                if (AM_Compare(currLeafBuf + prevKeyOff, attrType, attrLength, entries[i].key) == 0) {
                    is_dup = 1;
                }
            }

            /* Check if we must flush and allocate a new leaf page */
            int need_new_page = 0;
            if (currLeafPageNum < 0) {
                need_new_page = 1;
            } else if (!is_dup && (head.numKeys >= targetLeafKeys ||
                       (head.recIdPtr - head.keyPtr) < (recSize + recNodeSize))) {
                need_new_page = 1;
            } else if (is_dup && (head.recIdPtr - head.keyPtr) < recNodeSize) {
                /* Overflow guard for duplicate keys */
                need_new_page = 1;
            }

            if (need_new_page) {
                if (currLeafPageNum >= 0) {
                    /* Finalize current leaf header */
                    memcpy(currLeafBuf, &head, AM_sl);
                    PF_MarkDirty(indexFd, currLeafPageNum);
                    PF_UnfixPage(indexFd, currLeafPageNum, TRUE);
                    prevLeafPageNum = currLeafPageNum;
                }

                errVal = PF_AllocPage(indexFd, &currLeafPageNum, &currLeafBuf);
                AM_Check;

                head.pageType = 'l';
                head.nextLeafPage = AM_NULL_PAGE;
                head.recIdPtr = PF_PAGE_SIZE;
                head.keyPtr = AM_sl;
                head.freeListPtr = 0;
                head.numinfreeList = 0;
                head.attrLength = attrLength;
                head.numKeys = 0;
                head.maxKeys = maxLeafKeys;

                /* Link previous leaf's sibling pointer */
                if (prevLeafPageNum >= 0) {
                    char *prevBuf;
                    AM_LEAFHEADER prevHdr;
                    errVal = PF_GetThisPage(indexFd, prevLeafPageNum, &prevBuf);
                    AM_Check;
                    memcpy(&prevHdr, prevBuf, AM_sl);
                    prevHdr.nextLeafPage = currLeafPageNum;
                    memcpy(prevBuf, &prevHdr, AM_sl);
                    PF_MarkDirty(indexFd, prevLeafPageNum);
                    PF_UnfixPage(indexFd, prevLeafPageNum, TRUE);
                }

                /* Record this leaf's info for parent level */
                currLevelPages[currLevelCount].pageNum = currLeafPageNum;
                memcpy(currLevelPages[currLevelCount].subtreeMinKey, entries[i].key, attrLength);
                currLevelCount++;

                is_dup = 0; /* New page always starts with distinct key entry */
            }

            if (!is_dup) {
                /* New distinct key on leaf */
                head.recIdPtr -= recNodeSize;
                memcpy(currLeafBuf + head.recIdPtr, &entries[i].recId, sizeof(int));
                short nextPtr = 0;
                memcpy(currLeafBuf + head.recIdPtr + sizeof(int), &nextPtr, sizeof(short));

                memcpy(currLeafBuf + head.keyPtr, entries[i].key, attrLength);
                memcpy(currLeafBuf + head.keyPtr + attrLength, &head.recIdPtr, sizeof(short));
                head.keyPtr += recSize;
                head.numKeys++;
            } else {
                /* Duplicate key on current leaf: prepend recId to existing linked list */
                short oldRecIdPtr;
                short prevKeyOff = head.keyPtr - recSize;
                memcpy(&oldRecIdPtr, currLeafBuf + prevKeyOff + attrLength, sizeof(short));

                head.recIdPtr -= recNodeSize;
                memcpy(currLeafBuf + head.recIdPtr, &entries[i].recId, sizeof(int));
                memcpy(currLeafBuf + head.recIdPtr + sizeof(int), &oldRecIdPtr, sizeof(short));

                memcpy(currLeafBuf + prevKeyOff + attrLength, &head.recIdPtr, sizeof(short));
            }
        }

        if (currLeafPageNum >= 0) {
            memcpy(currLeafBuf, &head, AM_sl);
            PF_MarkDirty(indexFd, currLeafPageNum);
            PF_UnfixPage(indexFd, currLeafPageNum, TRUE);
        }
    }

    AM_LeftPageNum = currLevelPages[0].pageNum;

    /* 5. Recursively build parent internal index nodes bottom-up */
    nextLevelPages = (BulkPageInfo *)malloc(sizeof(BulkPageInfo) * (currLevelCount + 10));

    while (currLevelCount > 1) {
        int childIdx = 0;
        int recIntSize = attrLength + sizeof(int);
        nextLevelCount = 0;

        while (childIdx < currLevelCount) {
            int intPageNum;
            char *intPageBuf;
            AM_INTHEADER ihead;
            int firstChild = childIdx;
            int k = 0;

            errVal = PF_AllocPage(indexFd, &intPageNum, &intPageBuf);
            AM_Check;

            ihead.pageType = 'i';
            ihead.numKeys = 0;
            ihead.maxKeys = maxIntKeys;
            ihead.attrLength = attrLength;

            /* First child pointer at offset AM_sint */
            memcpy(intPageBuf + AM_sint, &currLevelPages[childIdx].pageNum, sizeof(int));
            childIdx++;

            /* Add subsequent children and their subtreeMinKey separators */
            while (childIdx < currLevelCount && k < maxIntKeys) {
                int keyOff = AM_sint + sizeof(int) + k * recIntSize;
                int childPtrOff = keyOff + attrLength;

                /* Propagate the leftmost leaf's key of this child subtree */
                memcpy(intPageBuf + keyOff, currLevelPages[childIdx].subtreeMinKey, attrLength);
                memcpy(intPageBuf + childPtrOff, &currLevelPages[childIdx].pageNum, sizeof(int));

                k++;
                childIdx++;
            }

            ihead.numKeys = k;
            memcpy(intPageBuf, &ihead, AM_sint);

            PF_MarkDirty(indexFd, intPageNum);
            PF_UnfixPage(indexFd, intPageNum, TRUE);

            nextLevelPages[nextLevelCount].pageNum = intPageNum;
            /* Parent inherits subtreeMinKey from its first child */
            memcpy(nextLevelPages[nextLevelCount].subtreeMinKey,
                   currLevelPages[firstChild].subtreeMinKey, attrLength);
            nextLevelCount++;
        }

        /* Promote next level to current level */
        memcpy(currLevelPages, nextLevelPages, sizeof(BulkPageInfo) * nextLevelCount);
        currLevelCount = nextLevelCount;
    }

    /* 6. Root Normalization: Copy the top-level root node into Page 0 */
    {
        int finalRootPageNum = currLevelPages[0].pageNum;
        char *finalRootBuf;
        char *page0Buf;

        if (finalRootPageNum != 0) {
            errVal = PF_GetThisPage(indexFd, finalRootPageNum, &finalRootBuf);
            AM_Check;
            errVal = PF_GetThisPage(indexFd, 0, &page0Buf);
            AM_Check;

            memcpy(page0Buf, finalRootBuf, PF_PAGE_SIZE);

            PF_MarkDirty(indexFd, 0);
            PF_UnfixPage(indexFd, 0, TRUE);
            PF_UnfixPage(indexFd, finalRootPageNum, FALSE);

            /* Free the temporary root page */
            PF_DisposePage(indexFd, finalRootPageNum);

            if (AM_LeftPageNum == finalRootPageNum) {
                AM_LeftPageNum = 0;
            }
        }
        AM_RootPageNum = 0;
    }

    PF_CloseFile(indexFd);
    free(currLevelPages);
    free(nextLevelPages);
    return AME_OK;
}
