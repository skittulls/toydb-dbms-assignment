#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ToyDBConfig {
    /* Buffer pool */
    int pool_size;
    int default_strategy; /* 0 for LRU, 1 for MRU */

    /* Workload */
    int working_set_pages;
    int cyclic_scan_pages;
    int num_queries;
    char query_mixes[256];

    /* Slotted page */
    char student_file[256];
    int static_thresholds[16];
    int num_static_thresholds;

    /* Bulk load */
    int leaf_fill_factor;
    char key_type[32]; /* "char" or "numeric" */

    /* Demo query */
    char demo_query_rollno[32];
} ToyDBConfig;

extern ToyDBConfig g_toydb_config;

/* Load configuration from path, or search default paths (./toydb.cfg, ../toydb.cfg) */
int config_load(const char *path);

/* Getters */
int cfg_get_pool_size(void);
int cfg_get_default_strategy(void);
int cfg_get_working_set_pages(void);
int cfg_get_cyclic_scan_pages(void);
int cfg_get_num_queries(void);
const char *cfg_get_student_file(void);
int cfg_get_leaf_fill_factor(void);
const char *cfg_get_key_type(void);
const char *cfg_get_demo_query_rollno(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_H */

