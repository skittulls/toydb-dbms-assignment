#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "student_loader.h"

int load_student_file(const char *student_path, int spFd, StudentRecordMeta **outMeta, int *outCount) {
    FILE *fp;
    char line[1024];
    int capacity = 20000;
    int count = 0;
    StudentRecordMeta *meta = NULL;

    int i;

    fp = fopen(student_path, "r");
    if (fp == NULL) {
        /* Try relative fallbacks */
        const char *fallbacks[] = {
            student_path,
            "data/student.txt",
            "../data/student.txt",
            "../../data/student.txt",
            NULL
        };
        for (i = 0; fallbacks[i] != NULL; i++) {
            fp = fopen(fallbacks[i], "r");
            if (fp != NULL) break;
        }
    }

    if (fp == NULL) {
        fprintf(stderr, "Error: Unable to open student data file at %s\n", student_path);
        return -1;
    }

    if (outMeta != NULL) {
        meta = (StudentRecordMeta *)malloc(sizeof(StudentRecordMeta) * capacity);
        if (meta == NULL) {
            fclose(fp);
            return -2;
        }
    }

    while (fgets(line, sizeof(line), fp)) {
        char *nl;
        int len;
        char copy[1024];
        char *token;
        RID rid;
        int err;

        /* Strip trailing newline */
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        len = strlen(line);
        if (len == 0) continue;
        if (strncmp(line, "Database dummy", 14) == 0) continue;

        /* Insert into slotted page file */
        err = SP_InsertRecord(spFd, line, len, &rid);
        if (err != PFE_OK) {
            fprintf(stderr, "Failed to insert record: %s\n", line);
            continue;
        }

        if (meta != NULL && count < capacity) {
            meta[count].recLen = len;
            meta[count].rid = rid;
            meta[count].id[0] = '\0';
            meta[count].roll_no[0] = '\0';

            /* Parse semicolon delimited fields: field 0 is ID, field 1 is roll-no */
            strcpy(copy, line);
            token = strtok(copy, ";");
            if (token != NULL) {
                strncpy(meta[count].id, token, sizeof(meta[count].id) - 1);
                token = strtok(NULL, ";");
                if (token != NULL) {
                    strncpy(meta[count].roll_no, token, sizeof(meta[count].roll_no) - 1);
                }
            }
        }
        count++;
    }

    fclose(fp);

    if (outMeta != NULL) {
        *outMeta = meta;
    }
    if (outCount != NULL) {
        *outCount = count;
    }
    return count;
}
