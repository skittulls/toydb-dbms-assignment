#ifndef SLOTTED_H
#define SLOTTED_H

#include "../pflayer/pf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 4-Byte bit-packed RID representation compatible with AM layer recId (int) */
typedef struct RID {
    int pageNum;   /* PF Page Number (16-bit effective) */
    short slotNum; /* Slot index within directory (16-bit effective) */
} RID;

#define RID_TO_INT(r)   ((((r).pageNum & 0xFFFF) << 16) | ((r).slotNum & 0xFFFF))
#define INT_TO_PAGE(x)  (((x) >> 16) & 0xFFFF)
#define INT_TO_SLOT(x)  ((short)((x) & 0xFFFF))

/* Slot directory entry */
typedef struct SPSlot {
    short offset; /* Byte offset to record payload within page */
    short length; /* Record length in bytes (-1 if deleted/tombstone) */
} SPSlot;

/* Slotted page header */
typedef struct SPHeader {
    short numSlots;         /* Total number of slot directory entries */
    short freeSpaceOffset;  /* Byte offset to top of free space (grows down from PF_PAGE_SIZE) */
    short freeSpaceBytes;   /* Total usable free bytes remaining in page */
    short numRecords;       /* Number of active (non-deleted) records */
} SPHeader;

/* Slotted Page File API */
int SP_CreateFile(char *fname);
int SP_DestroyFile(char *fname);
int SP_OpenFile(char *fname);
int SP_CloseFile(int spFd);

/* Record CRUD Operations */
int SP_InsertRecord(int spFd, char *recData, int recLen, RID *rid);
int SP_DeleteRecord(int spFd, RID rid);
int SP_GetRecord(int spFd, RID rid, char *buf, int *recLen);
int SP_CompactPage(char *pageBuf);

/* Sequential Scanning Cursor */
typedef struct SPScan {
    int spFd;
    int currPage;
    short currSlot;
} SPScan;

int SP_OpenScan(int spFd, SPScan *scan);
int SP_GetNextRecord(SPScan *scan, RID *rid, char *buf, int *recLen);
int SP_CloseScan(SPScan *scan);

/* Page statistics query */
typedef struct SPFileStats {
    int totalPages;
    int totalRecords;
    long totalPayloadBytes;
    long totalAllocatedBytes;
    double spaceUtilization;
} SPFileStats;

int SP_GetFileStats(int spFd, SPFileStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* SLOTTED_H */

