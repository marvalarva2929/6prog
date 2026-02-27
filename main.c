#include "libtdmm/tdmm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS    256
#define OPS      2000

static void run_workload(void) {
    void  *ptrs[SLOTS];
    memset(ptrs, 0, sizeof(ptrs));
    srand(12345);

    for (int op = 0; op < OPS; op++) {
        int slot = rand() % SLOTS;
        if (ptrs[slot]) {
            t_free(ptrs[slot]);
            ptrs[slot] = NULL;
        } else {
            size_t sz = (size_t)(rand() % 16384) + 8;
            ptrs[slot] = t_malloc(sz);
        }
    }

    for (int i = 0; i < SLOTS; i++)
        if (ptrs[i]) { t_free(ptrs[i]); ptrs[i] = NULL; }
}

int main(void) {
    alloc_strat_e strategies[] = { FIRST_FIT, BEST_FIT, WORST_FIT };
    const int     N            = 3;

    // 1 & 3: utilization + overhead over time 
    for (int i = 0; i < N; i++) {
        t_init(strategies[i]);
        run_workload();
        stats_write_utilization_csv("utilization_over_time.csv", i > 0);
        stats_write_overhead_csv   ("overhead_over_time.csv",    i > 0);
        stats_write_avg_util_csv   ("avg_utilization.csv",       i > 0);
        printf("Workload done: %s  events=%lu  avg_util=%.2f%%\n",
               i == 0 ? "FIRST_FIT" : i == 1 ? "BEST_FIT" : "WORST_FIT",
               g_stats.n_samples,
               g_stats.util_count > 0
                   ? g_stats.util_sum / g_stats.util_count * 100.0 : 0.0);
    }

    // 2: speed vs size (separate fresh heap per strategy) 
    for (int i = 0; i < N; i++) {
        stats_run_speed_benchmark(strategies[i]);
        stats_write_speed_csv("speed_vs_size.csv", i > 0);
        printf("Speed benchmark done: %s\n",
               i == 0 ? "FIRST_FIT" : i == 1 ? "BEST_FIT" : "WORST_FIT");
    }

    printf("\nCSVs written:\n");
    printf("  utilization_over_time.csv\n");
    printf("  overhead_over_time.csv\n");
    printf("  avg_utilization.csv\n");
    printf("  speed_vs_size.csv\n");
    printf("\nRun:  python3 plot.py\n");
    return 0;
}
