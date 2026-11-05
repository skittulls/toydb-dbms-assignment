#ifndef AMBULK_H
#define AMBULK_H

#include "am.h"
#include "pf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BulkEntry {
    char key[AM_MAXATTRLENGTH];
    int recId;
} BulkEntry;

typedef struct BulkPageInfo {
    int pageNum;
    char subtreeMinKey[AM_MAXATTRLENGTH];
} BulkPageInfo;

/* Bottom-up B+ tree bulk loading */
int AM_BulkLoad(char *fileName, int indexNo, char attrType, int attrLength,
                BulkEntry *entries, int numEntries, int fillFactorPercent);

#ifdef __cplusplus
}
#endif

#endif /* AMBULK_H */

