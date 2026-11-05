#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "static_record.h"

StaticEvalResult evaluate_static_allocation(const int *recordLengths, int count, int maxRecordLen) {
    StaticEvalResult res;
    int pageHeaderSize = sizeof(int); /* fixed page header stores record count or free pointer */
    int usableBytesPerPage = PF_PAGE_SIZE - pageHeaderSize;
    int recordsPerPage = usableBytesPerPage / maxRecordLen;
    int tailWastePerPage = usableBytesPerPage - (recordsPerPage * maxRecordLen);
    long totalPayload = 0;
    long internalFrag = 0;
    int i;

    memset(&res, 0, sizeof(res));
    res.maxRecordLen = maxRecordLen;
    res.totalRecords = count;
    res.recordsPerPage = recordsPerPage;

    if (recordsPerPage <= 0) {
        return res;
    }

    res.totalPages = (count + recordsPerPage - 1) / recordsPerPage;
    res.totalAllocatedBytes = (long)res.totalPages * PF_PAGE_SIZE;

    for (i = 0; i < count; i++) {
        int actualLen = recordLengths[i];
        if (actualLen > maxRecordLen) {
            actualLen = maxRecordLen; /* truncated to fit fixed slot */
        }
        totalPayload += actualLen;
        internalFrag += (maxRecordLen - actualLen);
    }

    res.usefulPayloadBytes = totalPayload;
    /* Total waste is internal padding within slots plus tail page fragmentation */
    res.wastedBytes = res.totalAllocatedBytes - totalPayload;
    res.internalFragPercent = (double)internalFrag * 100.0 / (double)((long)count * maxRecordLen);
    res.spaceUtilization = (double)totalPayload * 100.0 / (double)res.totalAllocatedBytes;

    return res;
}

