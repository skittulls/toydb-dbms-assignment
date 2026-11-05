/* bench_pf.c: Benchmark driver for PF layer enhancements.
   Evaluates LRU vs MRU under cyclic sequential scans, and evaluates
   workload mixtures (read/write queries) exporting metrics to CSV. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../pflayer/pf.h"
#include "../common/config.h"

#define BENCH_FNAME "bench_pf_temp.dat"

static void cleanup_temp_file(void) {
    unlink(BENCH_FNAME);
}

static void create_test_file(const char *fname, int num_pages) {
    int fd, i, page;
    char *buf;

    unlink(fname);
    if (PF_CreateFile((char *)fname) != PFE_OK) {
        fprintf(stderr, "Failed to create %s\n", fname);
        exit(1);
    }
    fd = PF_OpenFile((char *)fname);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n", fname);
        exit(1);
    }
    for (i = 0; i < num_pages; i++) {
        if (PF_AllocPage(fd, &page, &buf) != PFE_OK) {
            fprintf(stderr, "Failed to allocate page %d\n", i);
            exit(1);
        }
        buf[0] = (char)(i % 127);
        if (PF_UnfixPage(fd, page, TRUE) != PFE_OK) {
            fprintf(stderr, "Failed to unfix page %d\n", i);
            exit(1);
        }
    }
    PF_CloseFile(fd);
}

/* Experiment 1: Cyclic Sequential Scan (LRU vs MRU) */
static void run_cyclic_scan_experiment(void) {
    int num_pages = cfg_get_cyclic_scan_pages(); /* e.g. 25 */
    int pool_size = cfg_get_pool_size();         /* e.g. 20 */
    int passes = 10;
    int fd, p, pass;
    char *buf;
    PF_Stats lru_stats, mru_stats;

    printf("\n======================================================================\n");
    printf(" EXPERIMENT 1: Cyclic Sequential Scan (LRU vs MRU)\n");
    printf(" Working Set: %d pages | Buffer Pool: %d frames | Passes: %d\n", num_pages, pool_size, passes);
    printf("======================================================================\n");

    create_test_file(BENCH_FNAME, num_pages);

    /* 1. LRU */
    PF_ResetStats();
    fd = PF_OpenFileWithStrategy(BENCH_FNAME, PF_STRATEGY_LRU);
    for (pass = 0; pass < passes; pass++) {
        for (p = 0; p < num_pages; p++) {
            if (PF_GetThisPage(fd, p, &buf) == PFE_OK) {
                PF_UnfixPage(fd, p, FALSE);
            }
        }
    }
    PF_CloseFile(fd);
    PF_GetStats(&lru_stats);

    /* 2. MRU */
    PF_ResetStats();
    fd = PF_OpenFileWithStrategy(BENCH_FNAME, PF_STRATEGY_MRU);
    for (pass = 0; pass < passes; pass++) {
        for (p = 0; p < num_pages; p++) {
            if (PF_GetThisPage(fd, p, &buf) == PFE_OK) {
                PF_UnfixPage(fd, p, FALSE);
            }
        }
    }
    PF_CloseFile(fd);
    PF_GetStats(&mru_stats);

    printf("%-10s | %-12s | %-12s | %-12s | %-12s | %-10s\n",
           "Strategy", "Logical Reads", "Buffer Hits", "Buffer Misses", "Phys Reads", "Hit Ratio");
    printf("----------------------------------------------------------------------\n");
    printf("%-10s | %-12ld | %-12ld | %-12ld | %-12ld | %6.2f%%\n",
           "LRU", lru_stats.logical_reads, lru_stats.buffer_hits,
           lru_stats.buffer_misses, lru_stats.physical_reads,
           (lru_stats.logical_reads > 0) ? (double)lru_stats.buffer_hits * 100.0 / lru_stats.logical_reads : 0.0);
    printf("%-10s | %-12ld | %-12ld | %-12ld | %-12ld | %6.2f%%\n",
           "MRU", mru_stats.logical_reads, mru_stats.buffer_hits,
           mru_stats.buffer_misses, mru_stats.physical_reads,
           (mru_stats.logical_reads > 0) ? (double)mru_stats.buffer_hits * 100.0 / mru_stats.logical_reads : 0.0);
    printf("----------------------------------------------------------------------\n");
    printf("Observation: LRU suffers 0%% hit ratio on repeated passes due to sequential flooding.\n");
    printf("             MRU retains %d frames, yielding ~%.1f%% hit ratio.\n\n",
           pool_size - 1, (double)(pool_size - 1) * 100.0 / num_pages);

    cleanup_temp_file();
}

/* Experiment 2: Workload Mixtures across Read/Write ratios */
static void run_workload_mixtures(void) {
    int num_pages = cfg_get_working_set_pages(); /* e.g. 100 */
    int num_queries = cfg_get_num_queries();     /* e.g. 2000 */
    int fd, q;
    char *buf;
    FILE *csv;
    int read_pcts[] = {100, 80, 60, 50, 40, 20, 0};
    int write_pcts[] = {0, 20, 40, 50, 60, 80, 100};
    int num_mixes = 7;
    int m;

    printf("======================================================================\n");
    printf(" EXPERIMENT 2: Read/Write Query Mixtures\n");
    printf(" Working Set: %d pages | Buffer Pool: %d frames | Queries: %d\n",
           num_pages, cfg_get_pool_size(), num_queries);
    printf("======================================================================\n");

    create_test_file(BENCH_FNAME, num_pages);

    csv = fopen("results/pf_workload_metrics.csv", "w");
    if (csv == NULL) {
        csv = fopen("../results/pf_workload_metrics.csv", "w");
    }
    if (csv != NULL) {
        fprintf(csv, "ReadPct,WritePct,MixLabel,LogicalReads,LogicalWrites,TotalLogicalIO,PhysicalReads,PhysicalWrites,TotalPhysicalIO,BufferHits,BufferMisses,HitRatio\n");
    }

    printf("%-8s | %-10s | %-10s | %-10s | %-10s | %-10s | %-9s\n",
           "Mix (R/W)", "Log Reads", "Log Writes", "Phys Reads", "Phys Writes", "Total Phys", "Hit Ratio");
    printf("------------------------------------------------------------------------------\n");

    srand(42); /* deterministic workload */

    for (m = 0; m < num_mixes; m++) {
        int r_pct = read_pcts[m];
        int w_pct = write_pcts[m];
        PF_Stats stats;
        char label[32];
        sprintf(label, "%d/%d", r_pct, w_pct);

        PF_ResetStats();
        fd = PF_OpenFile(BENCH_FNAME);
        if (fd < 0) {
            fprintf(stderr, "Failed to open benchmark file\n");
            exit(1);
        }

        for (q = 0; q < num_queries; q++) {
            int p = rand() % num_pages;
            int is_write = ((rand() % 100) >= r_pct);

            if (PF_GetThisPage(fd, p, &buf) == PFE_OK) {
                if (is_write) {
                    buf[0]++;
                    PF_MarkDirty(fd, p);
                }
                PF_UnfixPage(fd, p, FALSE);
            }
        }

        PF_CloseFile(fd); /* Flushes dirty pages and records physical writes */
        PF_GetStats(&stats);

        {
            long total_logical = stats.logical_reads + stats.logical_writes;
            long total_phys = stats.physical_reads + stats.physical_writes;
            double hit_ratio = (stats.logical_reads > 0)
                ? (double)stats.buffer_hits * 100.0 / stats.logical_reads
                : 0.0;

            printf("%-8s | %-10ld | %-10ld | %-10ld | %-10ld | %-10ld | %7.2f%%\n",
                   label, stats.logical_reads, stats.logical_writes,
                   stats.physical_reads, stats.physical_writes, total_phys, hit_ratio);

            if (csv != NULL) {
                fprintf(csv, "%d,%d,%s,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%.2f\n",
                        r_pct, w_pct, label,
                        stats.logical_reads, stats.logical_writes, total_logical,
                        stats.physical_reads, stats.physical_writes, total_phys,
                        stats.buffer_hits, stats.buffer_misses, hit_ratio);
            }
        }
    }

    if (csv != NULL) {
        fclose(csv);
        printf("\nMetrics successfully exported to results/pf_workload_metrics.csv\n");
    }

    cleanup_temp_file();
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--config") == 0 && argc > 2) {
        config_load(argv[2]);
    } else {
        config_load(NULL);
    }

    PF_Init();

    printf("ToyDB PF Benchmark Suite Running...\n");
    printf("Configured Buffer Pool Size: %d frames\n", PF_GetBufferSize());

    run_cyclic_scan_experiment();
    run_workload_mixtures();

    printf("\nPF Layer Benchmark Completed Successfully.\n");
    return 0;
}

