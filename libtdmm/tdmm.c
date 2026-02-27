#include "tdmm.h"
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

const size_t HEAP_SIZE = 65563 + sizeof(memList);

alloc_strat_e  strategy;
memList       *list;
memList       *tail;
int            regions = 0;

allocator_stats_t g_stats;

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal stats helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static const char *strat_name(alloc_strat_e s) {
	switch(s) {
        case FIRST_FIT: return "first_fit";
        case BEST_FIT:  return "best_fit";
        case WORST_FIT: return "worst_fit";
        default:        return "unknown";
	}
}

void stats_reset(alloc_strat_e strat) {
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.strategy = strat;
}

static void record_sample(int is_alloc) {
    if (g_stats.n_samples >= MAX_SAMPLES) return;

    double util = (g_stats.system_bytes > 0)
        ? (double)g_stats.user_bytes / (double)g_stats.system_bytes
        : 0.0;

    stats_sample_t *s   = &g_stats.samples[g_stats.n_samples++];
    s->event_num        = g_stats.n_allocs + g_stats.n_frees;
    s->is_alloc         = is_alloc;
    s->user_bytes       = g_stats.user_bytes;
    s->system_bytes     = g_stats.system_bytes;
    s->utilization      = util;
    s->overhead_bytes   = g_stats.overhead_bytes;

    g_stats.util_sum   += util;
    g_stats.util_count++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Allocator internals
 * ═══════════════════════════════════════════════════════════════════════════ */

static void collapseFree(memList *ist) {
    memList *tlist = ist;
    if (tlist->header.isFree && tlist->next != NULL) {
        while (tlist->next != NULL
               && tlist->next->header.isFree
               && tlist->header.region == tlist->next->header.region) {
            memList *n = tlist->next;
            tlist->header.size += n->header.size + sizeof(memList);
            tlist->next = n->next;
            if (tlist->next)
                tlist->next->prev = tlist;
            else
                tail = tlist;

            /* node was merged away */
            if (g_stats.n_nodes) g_stats.n_nodes--;
            g_stats.overhead_bytes = g_stats.n_nodes * sizeof(memList);
        }
    }
}

static void allocateMem(memList *mem, size_t requestedSize) {
    if (!mem->header.isFree) return;

    size_t totalNeeded = requestedSize + sizeof(memList);
    size_t remaining   = mem->header.size - requestedSize;

    if (remaining > sizeof(memList)) {
        memList *newnext        = (memList *)((char *)mem + totalNeeded);
        newnext->header.isFree  = 1;
        newnext->header.size    = remaining - sizeof(memList);
        newnext->header.region  = mem->header.region;
        newnext->prev           = mem;
        newnext->next           = mem->next;
        if (newnext->next)
            newnext->next->prev = newnext;

        mem->next        = newnext;
        mem->header.size = requestedSize;
        if (tail == mem)
            tail = mem->next;

        /* new node created by split */
        g_stats.n_nodes++;
        g_stats.overhead_bytes = g_stats.n_nodes * sizeof(memList);
    }

    mem->header.isFree = 0;
}

static memList *getMem(size_t size) {
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "mmap failed\n");
        return NULL;
    }

    memList *ret        = (memList *)mem;
    ret->header.size    = size - sizeof(memList);
    ret->header.isFree  = 1;
    ret->header.region  = regions++;
    ret->next           = NULL;
    ret->prev           = NULL;

    /* track mmap */
    g_stats.system_bytes += size;
    g_stats.n_nodes++;
    g_stats.overhead_bytes = g_stats.n_nodes * sizeof(memList);

    return ret;
}

static void addList(memList *toAdd) {
    tail->next   = toAdd;
    toAdd->prev  = tail;
    tail         = tail->next;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Public allocator API
 * ═══════════════════════════════════════════════════════════════════════════ */

void t_init(alloc_strat_e strat) {
    strategy = strat;
    regions  = 0;
    stats_reset(strat);
    list = getMem(HEAP_SIZE);
    tail = list;
}

void *t_malloc(size_t requestedSize) {
    if (requestedSize == 0) return NULL;
    requestedSize = (requestedSize + 7) & ~7;  /* 8-byte align */

    memList *candidate = NULL;
    memList *tlist     = list;

    switch (strategy) {
        case FIRST_FIT:
            while (tlist) {
                if (tlist->header.isFree && tlist->header.size >= requestedSize) {
                    candidate = tlist; break;
                }
                tlist = tlist->next;
            }
            break;

        case BEST_FIT:
            while (tlist) {
                if (tlist->header.isFree && tlist->header.size >= requestedSize)
                    if (!candidate || tlist->header.size < candidate->header.size)
                        candidate = tlist;
                tlist = tlist->next;
            }
            break;

        case WORST_FIT:
            while (tlist) {
                if (tlist->header.isFree && tlist->header.size >= requestedSize)
                    if (!candidate || tlist->header.size > candidate->header.size)
                        candidate = tlist;
                tlist = tlist->next;
            }
            break;

        default: break;
    }

    if (!candidate) {
        memList *block = getMem(MAX(sizeof(memList) + requestedSize, HEAP_SIZE));
        if (!block) return NULL;
        addList(block);
        allocateMem(block, requestedSize);
        g_stats.user_bytes += requestedSize;
        g_stats.n_allocs++;
        record_sample(1);
        return (void *)(block + 1);
    }

    allocateMem(candidate, requestedSize);
    g_stats.user_bytes += requestedSize;
    g_stats.n_allocs++;
    record_sample(1);
    return (void *)(candidate + 1);
}

void t_free(void *ptr) {
    if (!ptr) return;

    memList *node = (memList *)ptr - 1;
    if (!node->header.isFree) {
        size_t freed = node->header.size;
        node->header.isFree = 1;
        collapseFree(node);
        if (node->prev && node->prev->header.isFree)
            collapseFree(node->prev);

        if (g_stats.user_bytes >= freed)
            g_stats.user_bytes -= freed;
        else
            g_stats.user_bytes = 0;
        g_stats.n_frees++;
        record_sample(0);
        return;
    }

    fprintf(stderr, "t_free: double-free or invalid pointer\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Speed benchmark
 * ═══════════════════════════════════════════════════════════════════════════ */

void stats_run_speed_benchmark(alloc_strat_e strat) {
    const int REPS = 500;
    t_init(strat);
    g_stats.n_speed = 0;

    for (int exp = 0; exp <= 23; exp++) {
        size_t sz             = (size_t)1 << exp;
        double malloc_total   = 0, free_total = 0;
        int    valid          = 0;

        for (int r = 0; r < REPS; r++) {
            double t0 = now_ns();
            void  *p  = t_malloc(sz);
            double t1 = now_ns();
            if (!p) continue;
            double t2 = now_ns();
            t_free(p);
            double t3 = now_ns();
            malloc_total += t1 - t0;
            free_total   += t3 - t2;
            valid++;
        }

        if (!valid) continue;
        speed_sample_t *sp = &g_stats.speed[g_stats.n_speed++];
        sp->request_size   = sz;
        sp->malloc_ns      = malloc_total / valid;
        sp->free_ns        = free_total   / valid;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CSV writers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* utilization_over_time.csv
   strategy, event_num, type, user_bytes, system_bytes, utilization_pct */
void stats_write_utilization_csv(const char *path, int append) {
    FILE *f = fopen(path, append ? "a" : "w");
    if (!f) { perror(path); return; }
    if (!append)
        fprintf(f, "strategy,event_num,type,user_bytes,system_bytes,utilization_pct\n");

    const char *sname = strat_name(g_stats.strategy);
    for (unsigned long i = 0; i < g_stats.n_samples; i++) {
        stats_sample_t *s = &g_stats.samples[i];
        fprintf(f, "%s,%lu,%s,%zu,%zu,%.4f\n",
                sname, s->event_num,
                s->is_alloc ? "alloc" : "free",
                s->user_bytes, s->system_bytes,
                s->utilization * 100.0);
    }
    fclose(f);
}

/* speed_vs_size.csv
   strategy, size_bytes, malloc_ns, free_ns */
void stats_write_speed_csv(const char *path, int append) {
    FILE *f = fopen(path, append ? "a" : "w");
    if (!f) { perror(path); return; }
    if (!append)
        fprintf(f, "strategy,size_bytes,malloc_ns,free_ns\n");

    const char *sname = strat_name(g_stats.strategy);
    for (int i = 0; i < g_stats.n_speed; i++) {
        speed_sample_t *sp = &g_stats.speed[i];
        fprintf(f, "%s,%zu,%.2f,%.2f\n",
                sname, sp->request_size, sp->malloc_ns, sp->free_ns);
    }
    fclose(f);
}

/* overhead_over_time.csv
   strategy, event_num, n_nodes, overhead_bytes, system_bytes, overhead_pct */
void stats_write_overhead_csv(const char *path, int append) {
    FILE *f = fopen(path, append ? "a" : "w");
    if (!f) { perror(path); return; }
    if (!append)
        fprintf(f, "strategy,event_num,n_nodes,overhead_bytes,system_bytes,overhead_pct\n");

    const char *sname = strat_name(g_stats.strategy);
    for (unsigned long i = 0; i < g_stats.n_samples; i++) {
        stats_sample_t *s = &g_stats.samples[i];
        double pct = s->system_bytes > 0
            ? (double)s->overhead_bytes / s->system_bytes * 100.0
            : 0.0;
        /* derive n_nodes from overhead_bytes at sample time */
        size_t n = (sizeof(memList) > 0) ? s->overhead_bytes / sizeof(memList) : 0;
        fprintf(f, "%s,%lu,%zu,%zu,%zu,%.4f\n",
                sname, s->event_num, n,
                s->overhead_bytes, s->system_bytes, pct);
    }
    fclose(f);
}

/* avg_utilization.csv
   strategy, avg_utilization_pct, total_allocs, total_frees, final_system_bytes */
void stats_write_avg_util_csv(const char *path, int append) {
    FILE *f = fopen(path, append ? "a" : "w");
    if (!f) { perror(path); return; }
    if (!append)
        fprintf(f, "strategy,avg_utilization_pct,total_allocs,total_frees,final_system_bytes\n");

    double avg = g_stats.util_count > 0
        ? g_stats.util_sum / (double)g_stats.util_count * 100.0
        : 0.0;
    fprintf(f, "%s,%.4f,%lu,%lu,%zu\n",
            strat_name(g_stats.strategy), avg,
            g_stats.n_allocs, g_stats.n_frees, g_stats.system_bytes);
    fclose(f);
}
