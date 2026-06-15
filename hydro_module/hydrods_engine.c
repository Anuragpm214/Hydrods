#include "hydrods_engine.h"
#include <string.h>
#include <stdio.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

hydro_bucket* create_bucket(int capacity) {
    hydro_bucket *b = (hydro_bucket*)malloc(sizeof(hydro_bucket));
    b->capacity = capacity;
    b->size = 0;
    b->data = (hydro_ts_node*)malloc(sizeof(hydro_ts_node) * capacity);
    b->max_ts = 0;
    return b;
}

void free_bucket(hydro_bucket *b) {
    if (b) {
        free(b->data);
        free(b);
    }
}

hydro_ds* hydrods_create(int bucket_capacity_limit) {
    hydro_ds *ds = (hydro_ds*)malloc(sizeof(hydro_ds));
    ds->bucket_capacity_limit = bucket_capacity_limit;
    ds->capacity = 4;
    ds->num_buckets = 0;
    ds->total_elements = 0;
    ds->eps_low = 0.50;
    ds->eps_high = 0.85;
    ds->buckets = (hydro_bucket**)malloc(sizeof(hydro_bucket*) * ds->capacity);
    return ds;
}

void hydrods_free(hydro_ds *ds) {
    if (!ds) return;
    for (int i = 0; i < ds->num_buckets; i++) {
        free_bucket(ds->buckets[i]);
    }
    free(ds->buckets);
    free(ds);
}

static inline double get_pressure(hydro_ds *ds, int idx) {
    if (idx < 0 || idx >= ds->num_buckets) return 0.0;
    return (double)(ds->buckets[idx]->size) / ds->bucket_capacity_limit;
}

static inline void update_index(hydro_bucket *b) {
    if (b->size > 0) {
        b->max_ts = b->data[b->size - 1].timestamp;
    }
}

static int find_bucket(hydro_ds *ds, uint64_t ts) {
    if (ds->num_buckets == 0) return 0;
    int l = 0, r = ds->num_buckets - 1;
    while (l <= r) {
        int m = l + (r - l) / 2;
        if (ds->buckets[m]->max_ts >= ts) {
            r = m - 1;
        } else {
            l = m + 1;
        }
    }
    if (l >= ds->num_buckets) l = ds->num_buckets - 1;
    return l;
}

static void flow(hydro_ds *ds, int i, int j) {
    double dp = get_pressure(ds, i) - get_pressure(ds, j);
    if (dp <= ds->eps_high) return;

    int k = (int)(ds->bucket_capacity_limit * (dp - ds->eps_low) / 2.0);
    if (k < 1) k = 1;
    if (k > ds->buckets[i]->size) k = ds->buckets[i]->size;

    hydro_bucket *A = ds->buckets[i];
    hydro_bucket *B = ds->buckets[j];

    // Ensure B has capacity
    if (B->size + k > B->capacity) {
        B->capacity *= 2;
        if (B->capacity < B->size + k) B->capacity = B->size + k;
        B->data = (hydro_ts_node*)realloc(B->data, sizeof(hydro_ts_node) * B->capacity);
    }

    if (i < j) {
        // Flow right: A's tail moves to B's head
        memmove(&B->data[k], &B->data[0], sizeof(hydro_ts_node) * B->size);
        memcpy(&B->data[0], &A->data[A->size - k], sizeof(hydro_ts_node) * k);
        B->size += k;
        A->size -= k;
    } else {
        // Flow left: A's head moves to B's tail
        memcpy(&B->data[B->size], &A->data[0], sizeof(hydro_ts_node) * k);
        B->size += k;
        memmove(&A->data[0], &A->data[k], sizeof(hydro_ts_node) * (A->size - k));
        A->size -= k;
    }

    update_index(A);
    update_index(B);
}

static void stabilize(hydro_ds *ds, int idx) {
    int active = 0;
    for (int step = 0; step < 2; ++step) {
        int moved = 0;
        if (idx > 0) {
            double dp = get_pressure(ds, idx) - get_pressure(ds, idx - 1);
            if ((!active && dp > ds->eps_high) || (active && dp >= ds->eps_low)) {
                active = 1;
                flow(ds, idx, idx - 1);
                moved = 1;
            }
        }
        if (idx + 1 < ds->num_buckets) {
            double dp = get_pressure(ds, idx) - get_pressure(ds, idx + 1);
            if ((!active && dp > ds->eps_high) || (active && dp >= ds->eps_low)) {
                active = 1;
                flow(ds, idx, idx + 1);
                moved = 1;
            }
        }
        if (!moved) break;
    }
}

static void split_bucket(hydro_ds *ds, int idx) {
    if (ds->num_buckets == ds->capacity) {
        ds->capacity *= 2;
        ds->buckets = (hydro_bucket**)realloc(ds->buckets, sizeof(hydro_bucket*) * ds->capacity);
    }

    hydro_bucket *B = ds->buckets[idx];
    int mid = B->size / 2;
    int move_count = B->size - mid;

    hydro_bucket *new_b = create_bucket(ds->bucket_capacity_limit);
    if (move_count > new_b->capacity) {
        new_b->capacity = move_count;
        new_b->data = (hydro_ts_node*)realloc(new_b->data, sizeof(hydro_ts_node) * new_b->capacity);
    }

    memcpy(new_b->data, &B->data[mid], sizeof(hydro_ts_node) * move_count);
    new_b->size = move_count;
    B->size = mid;

    memmove(&ds->buckets[idx + 2], &ds->buckets[idx + 1], sizeof(hydro_bucket*) * (ds->num_buckets - 1 - idx));
    ds->buckets[idx + 1] = new_b;
    ds->num_buckets++;

    update_index(B);
    update_index(new_b);
}

void hydrods_insert(hydro_ds *ds, uint64_t ts, double val) {
    if (ds->num_buckets == 0) {
        ds->buckets[0] = create_bucket(ds->bucket_capacity_limit);
        ds->buckets[0]->data[0].timestamp = ts;
        ds->buckets[0]->data[0].value = val;
        ds->buckets[0]->size = 1;
        ds->num_buckets = 1;
        ds->total_elements = 1;
        update_index(ds->buckets[0]);
        return;
    }

    int b_idx = find_bucket(ds, ts);
    hydro_bucket *B = ds->buckets[b_idx];

    // Find insert position using binary search
    int l = 0, r = B->size - 1;
    int insert_idx = B->size;
    while (l <= r) {
        int m = l + (r - l) / 2;
        if (B->data[m].timestamp >= ts) {
            insert_idx = m;
            r = m - 1;
        } else {
            l = m + 1;
        }
    }

    if (B->size == B->capacity) {
        B->capacity *= 2;
        B->data = (hydro_ts_node*)realloc(B->data, sizeof(hydro_ts_node) * B->capacity);
    }

    if (insert_idx < B->size) {
        memmove(&B->data[insert_idx + 1], &B->data[insert_idx], sizeof(hydro_ts_node) * (B->size - insert_idx));
    }
    
    B->data[insert_idx].timestamp = ts;
    B->data[insert_idx].value = val;
    B->size++;
    ds->total_elements++;

    update_index(B);

    if (B->size > ds->bucket_capacity_limit) {
        split_bucket(ds, b_idx);
    }

    stabilize(ds, b_idx);
}

// O(log N) binary search for lower bound
static inline int get_lower_bound(hydro_bucket *B, uint64_t ts) {
    int l = 0, r = B->size - 1;
    int ans = B->size;
    while (l <= r) {
        int m = l + (r - l) / 2;
        if (B->data[m].timestamp >= ts) {
            ans = m;
            r = m - 1;
        } else {
            l = m + 1;
        }
    }
    return ans;
}

// O(log N) binary search for upper bound
static inline int get_upper_bound(hydro_bucket *B, uint64_t ts) {
    int l = 0, r = B->size - 1;
    int ans = B->size;
    while (l <= r) {
        int m = l + (r - l) / 2;
        if (B->data[m].timestamp > ts) {
            ans = m;
            r = m - 1;
        } else {
            l = m + 1;
        }
    }
    return ans;
}

// Returns number of elements matched, fills *out_arr with contiguous blocks optionally
size_t hydrods_range_query(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts, hydro_ts_node **out_arr) {
    if (ds->num_buckets == 0 || start_ts > end_ts) return 0;

    int b_idx = find_bucket(ds, start_ts);
    size_t count = 0;
    
    // In a real module, out_arr would be a list of pointers to memory blocks to avoid memcpy.
    // For now, we just count them. 
    
    for (int i = b_idx; i < ds->num_buckets; ++i) {
        hydro_bucket *B = ds->buckets[i];
        if (B->size > 0 && B->data[0].timestamp > end_ts) break;

        int it_start = get_lower_bound(B, start_ts);
        int it_end = get_upper_bound(B, end_ts);

        count += (it_end - it_start);
    }

    return count;
}

hydro_agg_result hydrods_range_aggregate(hydro_ds *ds, uint64_t start_ts, uint64_t end_ts) {
    hydro_agg_result res = {0, 0.0, 1.7976931348623157e+308, -1.7976931348623157e+308}; // Use standard limits
    if (ds->num_buckets == 0 || start_ts > end_ts) return res;

    int b_idx = find_bucket(ds, start_ts);
    for (int i = b_idx; i < ds->num_buckets; ++i) {
        hydro_bucket *B = ds->buckets[i];
        if (B->size > 0 && B->data[0].timestamp > end_ts) break;

        int it_start = get_lower_bound(B, start_ts);
        int it_end = get_upper_bound(B, end_ts);

        for (int j = it_start; j < it_end; j++) {
            double val = B->data[j].value;
            res.sum += val;
            if (val < res.min) res.min = val;
            if (val > res.max) res.max = val;
        }
        res.count += (it_end - it_start);
    }
    return res;
}
