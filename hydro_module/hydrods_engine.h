#ifndef HYDRODS_ENGINE_H
#define HYDRODS_ENGINE_H

#include <stdint.h>
#include <stddef.h>

/*
 * Configurable Memory Allocator
 * When compiled as a Redis Module (-DREDIS_MODULE), all allocations go through
 * RedisModule_Alloc so that Redis can track memory usage via INFO, enforce
 * maxmemory policies, and trigger OOM handling correctly.
 * When compiled standalone, falls back to standard libc malloc/realloc/free.
 */
#ifdef REDIS_MODULE
#include "redismodule.h"
#define hydro_malloc(sz)        RedisModule_Alloc(sz)
#define hydro_realloc(ptr, sz)  RedisModule_Realloc(ptr, sz)
#define hydro_free(ptr)         RedisModule_Free(ptr)
#else
#include <stdlib.h>
#define hydro_malloc(sz)        malloc(sz)
#define hydro_realloc(ptr, sz)  realloc(ptr, sz)
#define hydro_free(ptr)         free(ptr)
#endif

// Represents a single 16-byte data point
typedef struct {
    uint64_t timestamp;
    double value;
} hydro_ts_node;

// Represents a fluid bucket
typedef struct {
    hydro_ts_node *data;
    int size;
    int capacity;
    uint64_t max_ts;
} hydro_bucket;

// The Core Data Structure
typedef struct {
    hydro_bucket **buckets;
    int num_buckets;
    int capacity;
    int bucket_capacity_limit;
    double eps_low;
    double eps_high;
    size_t total_elements;
    uint64_t retention_ms;
} hydro_ds;

typedef struct {
    size_t count;
    double sum;
    double min;
    double max;
} hydro_agg_result;

hydro_ds* hydrods_create(int bucket_capacity_limit, uint64_t retention_ms);
void hydrods_free(hydro_ds *ds);
void hydrods_enforce_retention(hydro_ds *ds, uint64_t current_ts);
int hydrods_insert(hydro_ds *ds, uint64_t timestamp, double value);  /* Returns 0 on success, -1 on allocation failure */
size_t hydrods_range_query(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts, hydro_ts_node **out_arr);
hydro_agg_result hydrods_range_aggregate(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts);

#endif // HYDRODS_ENGINE_H
