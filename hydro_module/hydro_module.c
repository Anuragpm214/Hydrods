#include "redismodule.h"
#include "hydrods_engine.h"
#include <stdlib.h>
#include <strings.h>
#include <string.h>

static RedisModuleType *HydroType;

// --- Native Data Type Methods ---

void HydroTypeFree(void *value) {
    hydrods_free((hydro_ds*)value);
}

void *HydroTypeRdbLoad(RedisModuleIO *rdb, int encver) {
    if (encver != 0 && encver != 1) {
        RedisModule_LogIOError(rdb, "warning", "Unsupported HydroDB encoding version %d", encver);
        return NULL;
    }
    
    uint64_t bucket_capacity_limit = RedisModule_LoadUnsigned(rdb);
    uint64_t total_elements = RedisModule_LoadUnsigned(rdb);
    
    uint64_t retention_ms = 0;
    if (encver >= 1) {
        retention_ms = RedisModule_LoadUnsigned(rdb);
    }
    
    hydro_ds *ds = hydrods_create((int)bucket_capacity_limit, retention_ms);
    if (!ds) return NULL;
    
    for (uint64_t i = 0; i < total_elements; i++) {
        uint64_t ts = RedisModule_LoadUnsigned(rdb);
        double val = RedisModule_LoadDouble(rdb);
        if (hydrods_insert(ds, ts, val) != 0) {
            hydrods_free(ds);
            return NULL;
        }
    }
    
    return ds;
}

void HydroTypeRdbSave(RedisModuleIO *rdb, void *value) {
    hydro_ds *ds = (hydro_ds*)value;
    
    RedisModule_SaveUnsigned(rdb, ds->bucket_capacity_limit);
    RedisModule_SaveUnsigned(rdb, ds->total_elements);
    RedisModule_SaveUnsigned(rdb, ds->retention_ms);
    
    for (int i = 0; i < ds->num_buckets; i++) {
        hydro_bucket *b = ds->buckets[i];
        for (int j = 0; j < b->size; j++) {
            RedisModule_SaveUnsigned(rdb, b->data[j].timestamp);
            RedisModule_SaveDouble(rdb, b->data[j].value);
        }
    }
}

void HydroTypeAofRewrite(RedisModuleIO *aof, RedisModuleString *key, void *value) {
    hydro_ds *ds = (hydro_ds*)value;
    
    for (int i = 0; i < ds->num_buckets; i++) {
        hydro_bucket *b = ds->buckets[i];
        for (int j = 0; j < b->size; j++) {
            char val_str[64];
            snprintf(val_str, sizeof(val_str), "%.17g", b->data[j].value);
            RedisModule_EmitAOF(aof, "HY.ADD", "sl c", key, (long long)b->data[j].timestamp, val_str);
        }
    }
}

size_t HydroTypeMemUsage(const void *value) {
    hydro_ds *ds = (hydro_ds*)value;
    size_t mem = sizeof(hydro_ds);
    mem += ds->capacity * sizeof(hydro_bucket*);
    for (int i = 0; i < ds->num_buckets; i++) {
        mem += sizeof(hydro_bucket);
        mem += ds->buckets[i]->capacity * sizeof(hydro_ts_node);
    }
    return mem;
}

// --- Commands ---

// HY.ADD key timestamp value [RETENTION ms]
int HyAdd_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 4 && argc != 6) return RedisModule_WrongArity(ctx);
    
    long long ts;
    double val;
    if (RedisModule_StringToLongLong(argv[2], &ts) != REDISMODULE_OK) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid timestamp");
    }
    if (ts < 0) {
        return RedisModule_ReplyWithError(ctx, "ERR timestamp must be non-negative");
    }
    if (RedisModule_StringToDouble(argv[3], &val) != REDISMODULE_OK) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid value");
    }

    long long retention_ms = 0;
    if (argc == 6) {
        size_t len;
        const char *arg4 = RedisModule_StringPtrLen(argv[4], &len);
        if (strcasecmp(arg4, "RETENTION") != 0) return RedisModule_ReplyWithError(ctx, "ERR syntax error");
        if (RedisModule_StringToLongLong(argv[5], &retention_ms) != REDISMODULE_OK || retention_ms < 0) {
            return RedisModule_ReplyWithError(ctx, "ERR invalid retention");
        }
    }

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && RedisModule_ModuleTypeGetType(key) != HydroType) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    hydro_ds *ds;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        ds = hydrods_create(1000, (uint64_t)retention_ms);
        if (!ds) {
            RedisModule_CloseKey(key);
            return RedisModule_ReplyWithError(ctx, "ERR out of memory");
        }
        RedisModule_ModuleTypeSetValue(key, HydroType, ds);
    } else {
        ds = RedisModule_ModuleTypeGetValue(key);
        if (argc == 6) {
            ds->retention_ms = (uint64_t)retention_ms; // Update retention if explicitly specified
        }
    }

    if (hydrods_insert(ds, (uint64_t)ts, val) != 0) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, "ERR out of memory during insert");
    }
    RedisModule_CloseKey(key);
    
    RedisModule_ReplicateVerbatim(ctx);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

// HY.MADD key ts val [ts val ...]
int HyMAdd_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc < 4 || argc % 2 != 0) return RedisModule_WrongArity(ctx);
    
    /* Pre-validate ALL arguments before modifying any data */
    for (int i = 2; i < argc; i += 2) {
        long long ts; double val;
        if (RedisModule_StringToLongLong(argv[i], &ts) != REDISMODULE_OK) {
            return RedisModule_ReplyWithError(ctx, "ERR invalid timestamp in MADD arguments");
        }
        if (ts < 0) {
            return RedisModule_ReplyWithError(ctx, "ERR timestamp must be non-negative");
        }
        if (RedisModule_StringToDouble(argv[i+1], &val) != REDISMODULE_OK) {
            return RedisModule_ReplyWithError(ctx, "ERR invalid value in MADD arguments");
        }
    }

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && RedisModule_ModuleTypeGetType(key) != HydroType) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    hydro_ds *ds;
    if (type == REDISMODULE_KEYTYPE_EMPTY) {
        ds = hydrods_create(1000, 0); // Default retention 0 for MADD right now
        if (!ds) {
            RedisModule_CloseKey(key);
            return RedisModule_ReplyWithError(ctx, "ERR out of memory");
        }
        RedisModule_ModuleTypeSetValue(key, HydroType, ds);
    } else {
        ds = RedisModule_ModuleTypeGetValue(key);
    }

    long long inserted = 0;
    for (int i = 2; i < argc; i += 2) {
        long long ts; double val;
        RedisModule_StringToLongLong(argv[i], &ts);
        RedisModule_StringToDouble(argv[i+1], &val);
        if (hydrods_insert(ds, (uint64_t)ts, val) != 0) {
            RedisModule_CloseKey(key);
            return RedisModule_ReplyWithError(ctx, "ERR out of memory during bulk insert");
        }
        inserted++;
    }
    
    RedisModule_CloseKey(key);
    RedisModule_ReplicateVerbatim(ctx);
    return RedisModule_ReplyWithLongLong(ctx, inserted);
}

// HY.RANGE key start_ts end_ts [AGGREGATION count|sum|min|max]
int HyRange_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 4 && argc != 6) return RedisModule_WrongArity(ctx);
    
    long long start_ts, end_ts;
    if (RedisModule_StringToLongLong(argv[2], &start_ts) != REDISMODULE_OK ||
        RedisModule_StringToLongLong(argv[3], &end_ts) != REDISMODULE_OK) {
        return RedisModule_ReplyWithError(ctx, "ERR invalid range");
    }
    if (start_ts < 0 || end_ts < 0) {
        return RedisModule_ReplyWithError(ctx, "ERR range timestamps must be non-negative");
    }

    const char *agg_type = "count"; // Default
    if (argc == 6) {
        size_t len;
        const char *arg4 = RedisModule_StringPtrLen(argv[4], &len);
        if (strcasecmp(arg4, "AGGREGATION") != 0) return RedisModule_ReplyWithError(ctx, "ERR syntax error");
        agg_type = RedisModule_StringPtrLen(argv[5], &len);
    }

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    if (RedisModule_KeyType(key) == REDISMODULE_KEYTYPE_EMPTY) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithArray(ctx, 0); // Empty array
    }
    if (RedisModule_ModuleTypeGetType(key) != HydroType) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    hydro_ds *ds = RedisModule_ModuleTypeGetValue(key);
    hydro_agg_result res = hydrods_range_aggregate(ds, (uint64_t)start_ts, (uint64_t)end_ts);
    RedisModule_CloseKey(key);

    if (res.count == 0) return RedisModule_ReplyWithNull(ctx);

    if (strcasecmp(agg_type, "count") == 0) return RedisModule_ReplyWithLongLong(ctx, res.count);
    else if (strcasecmp(agg_type, "sum") == 0) return RedisModule_ReplyWithDouble(ctx, res.sum);
    else if (strcasecmp(agg_type, "min") == 0) return RedisModule_ReplyWithDouble(ctx, res.min);
    else if (strcasecmp(agg_type, "max") == 0) return RedisModule_ReplyWithDouble(ctx, res.max);
    else return RedisModule_ReplyWithError(ctx, "ERR unknown aggregation type");
}

// HY.BENCHMARK key start_ts range_size queries
// NOTE: This command is for internal testing only. Consider disabling in production builds.
#include <sys/time.h>
int HyBench_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 5) return RedisModule_WrongArity(ctx);
    
    long long start_ts, range_size, queries;
    RedisModule_StringToLongLong(argv[2], &start_ts);
    RedisModule_StringToLongLong(argv[3], &range_size);
    RedisModule_StringToLongLong(argv[4], &queries);

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    if (RedisModule_KeyType(key) == REDISMODULE_KEYTYPE_EMPTY) {
        RedisModule_CloseKey(key);
        return RedisModule_ReplyWithError(ctx, "ERR key not found");
    }
    
    hydro_ds *ds = RedisModule_ModuleTypeGetValue(key);
    
    struct timeval tv_start, tv_end;
    gettimeofday(&tv_start, NULL);
    
    size_t dummy_sum = 0;
    for (int t = 0; t < queries; t++) {
        uint64_t s = start_ts + (t * 100) * 60;
        uint64_t e = s + range_size * 60;
        dummy_sum += hydrods_range_query(ds, s, e, NULL);
    }
    
    gettimeofday(&tv_end, NULL);
    RedisModule_CloseKey(key);
    
    double elapsed_ms = (tv_end.tv_sec - tv_start.tv_sec) * 1000.0 + (tv_end.tv_usec - tv_start.tv_usec) / 1000.0;
    
    char reply[128];
    snprintf(reply, sizeof(reply), "Algorithm Time for %lld queries: %.3f ms (checksum: %zu)", queries, elapsed_ms, dummy_sum);
    return RedisModule_ReplyWithSimpleString(ctx, reply);
}

// --- Module Initialization ---
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    REDISMODULE_NOT_USED(argv);
    REDISMODULE_NOT_USED(argc);

    if (RedisModule_Init(ctx, "hydro", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) {
        return REDISMODULE_ERR;
    }

    RedisModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = HydroTypeRdbLoad,
        .rdb_save = HydroTypeRdbSave,
        .aof_rewrite = HydroTypeAofRewrite,
        .mem_usage = HydroTypeMemUsage,
        .free = HydroTypeFree
    };

    HydroType = RedisModule_CreateDataType(ctx, "hydro-ts0", 1, &tm);
    if (HydroType == NULL) return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "hy.add", HyAdd_RedisCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx, "hy.madd", HyMAdd_RedisCommand, "write deny-oom", 1, 1, 1) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx, "hy.range", HyRange_RedisCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR) return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx, "hy.bench", HyBench_RedisCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR) return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
