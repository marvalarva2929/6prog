#ifndef TDMM_H
#define TDMM_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  FIRST_FIT,
  BEST_FIT,
  WORST_FIT,
} alloc_strat_e;

typedef struct Header {
	size_t size;
	int isFree;
	uint8_t region;
} Header;

typedef struct memList {
	struct memList *next, *prev;
	Header header;
} memList;


/**
 * Initializes the memory allocator with the given strategy.
 *
 * @param strat The strategy to use for memory allocation.
 */
void t_init(alloc_strat_e strat);

/**
 * Allocates a block of memory of the given size.
 *
 * @param size The size of the memory block to allocate.
 * @return A pointer to the allocated memory block fails.
 */
void *t_malloc(size_t size);

/**
 * Frees the given memory block.
 *
 * @param ptr The pointer to the memory block to free. This must be a pointer returned by t_malloc.
 */
void t_free(void *ptr);



typedef struct {
    unsigned long event_num;
    int           is_alloc;       /* 1 = alloc, 0 = free */
    size_t        user_bytes;     /* currently live user bytes */
    size_t        system_bytes;   /* total bytes obtained from OS so far */
    double        utilization;    /* user_bytes / system_bytes */
    size_t        overhead_bytes; /* total memList node bytes in use */
} stats_sample_t;

typedef struct {
    size_t request_size;
    double malloc_ns;
    double free_ns;
} speed_sample_t;

#define MAX_SAMPLES 131072
#define MAX_SPEED   24   /* 2^0 .. 2^23 */

typedef struct {
    stats_sample_t samples[MAX_SAMPLES];
    unsigned long  n_samples;

    size_t         user_bytes;
    size_t         system_bytes;
    unsigned long  n_allocs;
    unsigned long  n_frees;

    double         util_sum;
    unsigned long  util_count;

    size_t         n_nodes;
    size_t         overhead_bytes;

    speed_sample_t speed[MAX_SPEED];
    int            n_speed;

    alloc_strat_e  strategy;
} allocator_stats_t;

extern allocator_stats_t g_stats;

void stats_reset(alloc_strat_e strat);
void stats_run_speed_benchmark(alloc_strat_e strat);

/* Write results to CSV files. Pass append=0 for first strategy, 1 after. */
void stats_write_utilization_csv(const char *path, int append);
void stats_write_speed_csv(const char *path, int append);
void stats_write_overhead_csv(const char *path, int append);
void stats_write_avg_util_csv(const char *path, int append);


#endif // TDMM_H
