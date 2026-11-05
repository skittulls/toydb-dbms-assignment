#ifndef STUDENT_LOADER_H
#define STUDENT_LOADER_H

#include "slotted.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StudentRecordMeta {
    char id[32];
    char roll_no[32];
    int recLen;
    RID rid;
} StudentRecordMeta;

/* Load all student records from student_file into an open slotted page file */
int load_student_file(const char *student_path, int spFd, StudentRecordMeta **outMeta, int *outCount);

#ifdef __cplusplus
}
#endif

#endif /* STUDENT_LOADER_H */

