#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"

ToyDBConfig g_toydb_config;
static int g_config_initialized = 0;

static void trim(char *str) {
    char *end;
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
}

/* Normalize identifier: lowercase and strip '_', '-', ' ', '[', ']' */
static void normalize_name(const char *src, char *dst, size_t dst_size) {
    size_t i = 0;
    while (*src && i + 1 < dst_size) {
        unsigned char c = (unsigned char)*src++;
        if (c != '_' && c != '-' && c != ' ' && c != '\t' && c != '[' && c != ']') {
            dst[i++] = (char)tolower(c);
        }
    }
    dst[i] = '\0';
}

static void set_defaults(void) {
    g_toydb_config.pool_size = 20;
    g_toydb_config.default_strategy = 0; /* LRU */
    g_toydb_config.working_set_pages = 100;
    g_toydb_config.cyclic_scan_pages = 25;
    g_toydb_config.num_queries = 2000;
    strcpy(g_toydb_config.query_mixes, "100/0, 80/20, 60/40, 50/50, 40/60, 20/80, 0/100");
    strcpy(g_toydb_config.student_file, "data/student.txt");
    g_toydb_config.static_thresholds[0] = 108;
    g_toydb_config.static_thresholds[1] = 128;
    g_toydb_config.static_thresholds[2] = 160;
    g_toydb_config.static_thresholds[3] = 200;
    g_toydb_config.static_thresholds[4] = 256;
    g_toydb_config.static_thresholds[5] = 512;
    g_toydb_config.num_static_thresholds = 6;
    g_toydb_config.leaf_fill_factor = 90;
    strcpy(g_toydb_config.key_type, "char");
    strcpy(g_toydb_config.demo_query_rollno, "95302001");
}

int config_load(const char *path) {
    FILE *fp = NULL;
    char line[512];
    char current_section[64];
    const char *candidates[] = {
        path,
        "toydb.cfg",
        "../toydb.cfg",
        "../../toydb.cfg",
        NULL
    };
    int i;

    set_defaults();
    current_section[0] = '\0';

    for (i = 0; candidates[i] != NULL; i++) {
        if (candidates[i] != NULL && candidates[i][0] != '\0') {
            fp = fopen(candidates[i], "r");
            if (fp != NULL) break;
        }
    }

    if (fp == NULL) {
        g_config_initialized = 1;
        return 0; /* Use defaults */
    }

    while (fgets(line, sizeof(line), fp)) {
        char *eq;
        trim(line);
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        /* Check for INI section headers like [BufferPool] or [buffer_pool] */
        if (line[0] == '[') {
            normalize_name(line, current_section, sizeof(current_section));
            continue;
        }

        eq = strchr(line, '=');
        if (eq == NULL) continue;
        *eq = '\0';
        {
            char *key = line;
            char *val = eq + 1;
            char norm_key[64];
            char *hash = strchr(val, '#');
            if (hash) *hash = '\0';
            trim(key);
            trim(val);

            normalize_name(key, norm_key, sizeof(norm_key));

            if (strcmp(norm_key, "poolsize") == 0 || strcmp(key, "pool_size") == 0) {
                int ps = atoi(val);
                if (ps >= 3) g_toydb_config.pool_size = ps;
            } else if (strcmp(norm_key, "defaultstrategy") == 0 || strcmp(key, "default_strategy") == 0) {
                /* Handles both strings (LRU/MRU) and integers (0/1) */
                if (strcasecmp(val, "MRU") == 0 || strcmp(val, "1") == 0 ||
                    strcasecmp(val, "PF_STRATEGY_MRU") == 0) {
                    g_toydb_config.default_strategy = 1;
                } else if (strcasecmp(val, "LRU") == 0 || strcmp(val, "0") == 0 ||
                           strcasecmp(val, "PF_STRATEGY_LRU") == 0) {
                    g_toydb_config.default_strategy = 0;
                } else {
                    int s = atoi(val);
                    g_toydb_config.default_strategy = (s == 1) ? 1 : 0;
                }
            } else if (strcmp(norm_key, "workingsetpages") == 0 || strcmp(key, "working_set_pages") == 0) {
                int wsp = atoi(val);
                if (wsp > 0) g_toydb_config.working_set_pages = wsp;
            } else if (strcmp(norm_key, "cyclicscanpages") == 0 || strcmp(key, "cyclic_scan_pages") == 0) {
                int csp = atoi(val);
                if (csp > 0) g_toydb_config.cyclic_scan_pages = csp;
            } else if (strcmp(norm_key, "numqueries") == 0 || strcmp(key, "num_queries") == 0) {
                int nq = atoi(val);
                if (nq > 0) g_toydb_config.num_queries = nq;
            } else if (strcmp(norm_key, "querymixes") == 0 || strcmp(key, "query_mixes") == 0) {
                strncpy(g_toydb_config.query_mixes, val, sizeof(g_toydb_config.query_mixes) - 1);
            } else if (strcmp(norm_key, "studentfile") == 0 || strcmp(key, "student_file") == 0) {
                strncpy(g_toydb_config.student_file, val, sizeof(g_toydb_config.student_file) - 1);
            } else if (strcmp(norm_key, "staticthresholds") == 0 || strcmp(key, "static_thresholds") == 0) {
                char temp[256];
                char *token;
                int count = 0;
                strncpy(temp, val, sizeof(temp) - 1);
                temp[sizeof(temp) - 1] = '\0';
                token = strtok(temp, ",");
                while (token != NULL && count < 16) {
                    trim(token);
                    if (*token) {
                        g_toydb_config.static_thresholds[count++] = atoi(token);
                    }
                    token = strtok(NULL, ",");
                }
                if (count > 0) g_toydb_config.num_static_thresholds = count;
            } else if (strcmp(norm_key, "leaffillfactor") == 0 || strcmp(key, "leaf_fill_factor") == 0) {
                int ff = atoi(val);
                if (ff >= 10 && ff <= 100) g_toydb_config.leaf_fill_factor = ff;
            } else if (strcmp(norm_key, "keytype") == 0 || strcmp(key, "key_type") == 0) {
                strncpy(g_toydb_config.key_type, val, sizeof(g_toydb_config.key_type) - 1);
            } else if (strcmp(norm_key, "demoqueryrollno") == 0 || strcmp(key, "demo_query_rollno") == 0) {
                strncpy(g_toydb_config.demo_query_rollno, val, sizeof(g_toydb_config.demo_query_rollno) - 1);
            }
        }
    }

    fclose(fp);
    g_config_initialized = 1;
    return 1;
}

int cfg_get_pool_size(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.pool_size;
}

int cfg_get_default_strategy(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.default_strategy;
}

int cfg_get_working_set_pages(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.working_set_pages;
}

int cfg_get_cyclic_scan_pages(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.cyclic_scan_pages;
}

int cfg_get_num_queries(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.num_queries;
}

const char *cfg_get_student_file(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.student_file;
}

int cfg_get_leaf_fill_factor(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.leaf_fill_factor;
}

const char *cfg_get_key_type(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.key_type;
}

const char *cfg_get_demo_query_rollno(void) {
    if (!g_config_initialized) config_load(NULL);
    return g_toydb_config.demo_query_rollno;
}
