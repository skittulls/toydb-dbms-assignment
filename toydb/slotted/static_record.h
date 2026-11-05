#ifndef STATIC_RECORD_H
#define STATIC_RECORD_H

#include "../pflayer/pf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StaticEvalResult {
    int maxRecordLen;
    int totalRecords;
    int recordsPerPage;
    int totalPages;
    long totalAllocatedBytes;
    long usefulPayloadBytes;
    long wastedBytes;
    double internalFragPercent;
    double spaceUtilization;
} StaticEvalResult;

/* Simulate static fixed-slot allocation for a dataset of record lengths */
StaticEvalResult evaluate_static_allocation(const int *recordLengths, int count, int maxRecordLen);

#ifdef __cplusplus
}
#endif

#endif /* STATIC_RECORD_H */

