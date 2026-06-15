#ifndef HYDRODS_ENGINE_H
#define HYDRODS_ENGINE_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

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
} hydro_ds;

typedef struct {
    size_t count;
    double sum;
    double min;
    double max;
} hydro_agg_result;

hydro_ds* hydrods_create(int bucket_capacity_limit);
void hydrods_free(hydro_ds *ds);
void hydrods_insert(hydro_ds *ds, uint64_t timestamp, double value);
size_t hydrods_range_query(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts, hydro_ts_node **out_arr);
hydro_agg_result hydrods_range_aggregate(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts);

#endif // HYDRODS_ENGINE_H
