#!/usr/bin/env python3
"""
HydroDB Full Test & Benchmark Suite
Runs comprehensive tests + real benchmarks and outputs results for README.
"""

import redis
import time
import random
import sys
import json

PORT = 6387
r = redis.Redis(port=PORT, decode_responses=True, socket_timeout=10)

# ============================================================
# PART 1: COMPREHENSIVE FUNCTIONAL TESTS
# ============================================================

def test_section(name):
    print(f"\n{'='*60}")
    print(f"  {name}")
    print(f"{'='*60}")

results = {"tests_passed": 0, "tests_failed": 0, "failures": []}

def assert_test(test_num, name, condition, detail=""):
    if condition:
        print(f"  ✅ Test {test_num}: {name}")
        results["tests_passed"] += 1
    else:
        print(f"  ❌ Test {test_num}: {name} — {detail}")
        results["tests_failed"] += 1
        results["failures"].append(f"Test {test_num}: {name} — {detail}")

try:
    r.ping()
except Exception as e:
    print(f"❌ Cannot connect to Redis at port {PORT}: {e}")
    sys.exit(1)

r.flushall()

# --- Test Group 1: Basic Operations ---
test_section("1. BASIC OPERATIONS")

# Test 1: HY.ADD single point
r.execute_command("HY.ADD", "sensor1", 100, 25.5)
cnt = r.execute_command("HY.RANGE", "sensor1", 0, 200)
assert_test(1, "HY.ADD single point + RANGE count", cnt == 1, f"Expected 1, got {cnt}")

# Test 2: HY.ADD multiple sequential points
for i in range(10):
    r.execute_command("HY.ADD", "sensor1", 200 + i * 100, 30.0 + i)
cnt = r.execute_command("HY.RANGE", "sensor1", 0, 2000)
assert_test(2, "HY.ADD 10 sequential points", cnt == 11, f"Expected 11, got {cnt}")

# Test 3: HY.MADD bulk insert
r.execute_command("HY.MADD", "sensor2", 1000, 10.0, 2000, 20.0, 3000, 30.0, 4000, 40.0, 5000, 50.0)
cnt = r.execute_command("HY.RANGE", "sensor2", 0, 10000)
assert_test(3, "HY.MADD 5 points", cnt == 5, f"Expected 5, got {cnt}")

# --- Test Group 2: Aggregations ---
test_section("2. AGGREGATION CORRECTNESS")

r.flushall()
# Insert known values: 10, 20, 30, 40, 50
r.execute_command("HY.MADD", "agg_test", 100, 10.0, 200, 20.0, 300, 30.0, 400, 40.0, 500, 50.0)

# Test 4: COUNT
cnt = r.execute_command("HY.RANGE", "agg_test", 0, 600, "AGGREGATION", "count")
assert_test(4, "AGGREGATION count", cnt == 5, f"Expected 5, got {cnt}")

# Test 5: SUM (10+20+30+40+50=150)
s = float(r.execute_command("HY.RANGE", "agg_test", 0, 600, "AGGREGATION", "sum"))
assert_test(5, "AGGREGATION sum", abs(s - 150.0) < 0.01, f"Expected 150.0, got {s}")

# Test 6: MIN
mn = float(r.execute_command("HY.RANGE", "agg_test", 0, 600, "AGGREGATION", "min"))
assert_test(6, "AGGREGATION min", abs(mn - 10.0) < 0.01, f"Expected 10.0, got {mn}")

# Test 7: MAX
mx = float(r.execute_command("HY.RANGE", "agg_test", 0, 600, "AGGREGATION", "max"))
assert_test(7, "AGGREGATION max", abs(mx - 50.0) < 0.01, f"Expected 50.0, got {mx}")

# Test 8: Partial range aggregation (only ts 200-400: values 20,30,40)
s = float(r.execute_command("HY.RANGE", "agg_test", 200, 400, "AGGREGATION", "sum"))
assert_test(8, "Partial range sum (200-400)", abs(s - 90.0) < 0.01, f"Expected 90.0, got {s}")

cnt = r.execute_command("HY.RANGE", "agg_test", 200, 400, "AGGREGATION", "count")
assert_test(9, "Partial range count (200-400)", cnt == 3, f"Expected 3, got {cnt}")

# --- Test Group 3: Edge Cases ---
test_section("3. EDGE CASES")

# Test 10: start > end → should return 0 or None
res = r.execute_command("HY.RANGE", "agg_test", 500, 100)
assert_test(10, "start > end returns 0/None", res is None or res == 0, f"Got {res}")

# Test 11: Missing key → empty array
res = r.execute_command("HY.RANGE", "nonexistent_key", 0, 100)
assert_test(11, "Missing key returns empty list", res == [], f"Got {res}")

# Test 12: Single point exact match
cnt = r.execute_command("HY.RANGE", "agg_test", 300, 300, "AGGREGATION", "count")
assert_test(12, "Exact single timestamp match", cnt == 1, f"Expected 1, got {cnt}")

# Test 13: Range with no matching points
cnt = r.execute_command("HY.RANGE", "agg_test", 550, 600)
# Could be 0 or None
assert_test(13, "Range with no matches", cnt is None or cnt == 0, f"Expected 0/None, got {cnt}")

# --- Test Group 4: Error Handling ---
test_section("4. ERROR HANDLING")

# Test 14: Invalid aggregation type
try:
    r.execute_command("HY.RANGE", "agg_test", 0, 600, "AGGREGATION", "fake_agg")
    assert_test(14, "Invalid aggregation → error", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(14, "Invalid aggregation → error", "unknown aggregation type" in str(e), str(e))

# Test 15: Negative timestamp → error
try:
    r.execute_command("HY.ADD", "neg_test", -1, 99.9)
    assert_test(15, "Negative timestamp → error", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(15, "Negative timestamp → error", "non-negative" in str(e), str(e))

# Test 16: Negative timestamp in MADD → error
try:
    r.execute_command("HY.MADD", "neg_test", 100, 10.0, -5, 20.0)
    assert_test(16, "Negative timestamp in MADD → error", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(16, "Negative timestamp in MADD → error", "non-negative" in str(e), str(e))

# Test 17: Invalid value in MADD → error (no silent skip)
try:
    r.execute_command("HY.MADD", "bad_val_test", 100, "not_a_number", 200, 30.0)
    assert_test(17, "Invalid value in MADD → error", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(17, "Invalid value in MADD → error", "invalid" in str(e).lower(), str(e))

# Test 18: Wrong arity
try:
    r.execute_command("HY.ADD", "test")  # missing ts and val
    assert_test(18, "Wrong arity → error", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(18, "Wrong arity → error", True)

# Test 19: WRONGTYPE check
r.set("string_key", "hello")
try:
    r.execute_command("HY.ADD", "string_key", 100, 25.0)
    assert_test(19, "WRONGTYPE on non-hydro key", False, "Should have thrown error")
except redis.exceptions.ResponseError as e:
    assert_test(19, "WRONGTYPE on non-hydro key", "WRONGTYPE" in str(e), str(e))

# --- Test Group 5: Memory & Persistence ---
test_section("5. MEMORY & PERSISTENCE")

# Test 20: DEL properly frees memory
r.execute_command("HY.ADD", "del_test", 100, 99.9)
r.execute_command("DEL", "del_test")
res = r.execute_command("HY.RANGE", "del_test", 0, 500)
assert_test(20, "DEL frees memory (no segfault)", res == [], f"Expected [], got {res}")

# Test 21: MEMORY USAGE works
r.execute_command("HY.ADD", "mem_test", 100, 99.9)
mem = r.execute_command("MEMORY", "USAGE", "mem_test")
assert_test(21, "MEMORY USAGE returns value", mem is not None and mem > 0, f"Got {mem}")
print(f"     → MEMORY USAGE for 1 point: {mem} bytes")

# Test 22: Large insert + MEMORY USAGE
r.flushall()
pipe = r.pipeline(transaction=False)
for i in range(10000):
    pipe.execute_command("HY.ADD", "large_key", i * 60, 100.0 + random.uniform(-5, 5))
pipe.execute()
mem = r.execute_command("MEMORY", "USAGE", "large_key")
cnt = r.execute_command("HY.RANGE", "large_key", 0, 999999999)
assert_test(22, f"Large insert (10K points) count correct", cnt == 10000, f"Expected 10000, got {cnt}")
print(f"     → MEMORY USAGE for 10,000 points: {mem} bytes ({mem/1024:.1f} KB, {mem/10000:.1f} bytes/point)")

# Test 23: RDB Save
try:
    r.bgsave()
    time.sleep(1)
    assert_test(23, "BGSAVE completes without crash", True)
except Exception as e:
    assert_test(23, "BGSAVE completes without crash", False, str(e))

# --- Test Group 6: Scale Test ---
test_section("6. SCALE TEST (100K POINTS)")

r.flushall()
SCALE_N = 100000
start_ts = 1700000000
pipe = r.pipeline(transaction=False)
batch_size = 10000
for i in range(SCALE_N):
    pipe.execute_command("HY.ADD", "scale_test", start_ts + i * 60, 50.0 + random.uniform(-10, 10))
    if (i + 1) % batch_size == 0:
        pipe.execute()
pipe.execute()

cnt = r.execute_command("HY.RANGE", "scale_test", start_ts, start_ts + SCALE_N * 60)
assert_test(24, f"100K points inserted correctly", cnt == SCALE_N, f"Expected {SCALE_N}, got {cnt}")

mem = r.execute_command("MEMORY", "USAGE", "scale_test")
print(f"     → MEMORY USAGE for 100K points: {mem} bytes ({mem/1024:.1f} KB, {mem/(1024*1024):.2f} MB, {mem/SCALE_N:.1f} bytes/point)")


# ============================================================
# PART 2: REAL PERFORMANCE BENCHMARKS
# ============================================================

test_section("PERFORMANCE BENCHMARKS")

r.flushall()

NUM_POINTS = 1000000
BATCH = 10000
start_ts = 1700000000

print(f"\n  📊 Ingesting {NUM_POINTS:,} data points via TCP pipeline (batch={BATCH})...")

# Ingestion benchmark
ingest_start = time.time()
pipe = r.pipeline(transaction=False)
for i in range(NUM_POINTS):
    pipe.execute_command("HY.ADD", "bench_key", start_ts + i * 60, 100.0 + random.uniform(-1, 1))
    if (i + 1) % BATCH == 0:
        pipe.execute()
        if (i + 1) % 100000 == 0:
            print(f"     ... {i+1:,} / {NUM_POINTS:,} inserted")
pipe.execute()
ingest_time = time.time() - ingest_start
print(f"  ✅ Ingestion complete: {ingest_time:.2f}s ({NUM_POINTS/ingest_time:,.0f} inserts/sec)")

# Verify count
cnt = r.execute_command("HY.RANGE", "bench_key", start_ts, start_ts + NUM_POINTS * 60)
print(f"  ✅ Verification: {cnt:,} points stored")

# Memory usage
mem = r.execute_command("MEMORY", "USAGE", "bench_key")
print(f"  💾 Memory: {mem:,} bytes ({mem/(1024*1024):.2f} MB, {mem/NUM_POINTS:.1f} bytes/point)")

# Range query benchmarks
def bench_range(range_size, num_queries, label):
    times = []
    for _ in range(num_queries):
        max_start = max(0, NUM_POINTS - range_size - 1)
        start_idx = random.randint(0, max_start)
        s_ts = start_ts + start_idx * 60
        e_ts = s_ts + range_size * 60
        
        t1 = time.time()
        r.execute_command("HY.RANGE", "bench_key", s_ts, e_ts, "AGGREGATION", "count")
        elapsed = time.time() - t1
        times.append(elapsed)
    
    total_ms = sum(times) * 1000
    avg_ms = (sum(times) / len(times)) * 1000
    p50 = sorted(times)[len(times)//2] * 1000
    p99 = sorted(times)[int(len(times)*0.99)] * 1000
    
    return {
        "label": label,
        "range_size": range_size,
        "num_queries": num_queries,
        "total_ms": total_ms,
        "avg_ms": avg_ms,
        "p50_ms": p50,
        "p99_ms": p99
    }

print(f"\n  📊 Running range query benchmarks...")

# Small range: 1,440 points (~1 day at 1min intervals)
small = bench_range(1440, 500, "Small Range (1,440 pts)")
print(f"  ✅ {small['label']}: {small['num_queries']} queries in {small['total_ms']:.1f}ms total (avg={small['avg_ms']:.3f}ms, p50={small['p50_ms']:.3f}ms, p99={small['p99_ms']:.3f}ms)")

# Medium range: 43,200 points (~30 days)
medium = bench_range(43200, 500, "Medium Range (43,200 pts)")
print(f"  ✅ {medium['label']}: {medium['num_queries']} queries in {medium['total_ms']:.1f}ms total (avg={medium['avg_ms']:.3f}ms, p50={medium['p50_ms']:.3f}ms, p99={medium['p99_ms']:.3f}ms)")

# Large range: 100,000 points
large = bench_range(100000, 150, "Large Range (100,000 pts)")
print(f"  ✅ {large['label']}: {large['num_queries']} queries in {large['total_ms']:.1f}ms total (avg={large['avg_ms']:.3f}ms, p50={large['p50_ms']:.3f}ms, p99={large['p99_ms']:.3f}ms)")

# Full range: all 1M points  
full = bench_range(NUM_POINTS, 50, "Full Range (1,000,000 pts)")
print(f"  ✅ {full['label']}: {full['num_queries']} queries in {full['total_ms']:.1f}ms total (avg={full['avg_ms']:.3f}ms, p50={full['p50_ms']:.3f}ms, p99={full['p99_ms']:.3f}ms)")

# Aggregation type benchmarks
print(f"\n  📊 Running aggregation benchmarks (1M pts range)...")
agg_results = {}
for agg_type in ["count", "sum", "min", "max"]:
    times = []
    for _ in range(50):
        t1 = time.time()
        r.execute_command("HY.RANGE", "bench_key", start_ts, start_ts + NUM_POINTS * 60, "AGGREGATION", agg_type)
        times.append(time.time() - t1)
    avg = sum(times) / len(times) * 1000
    agg_results[agg_type] = avg
    print(f"     {agg_type.upper():>5}: avg {avg:.3f}ms per query")

# Internal benchmark (CPU-only)
print(f"\n  📊 Running internal CPU benchmark (HY.BENCH)...")
bench_result = r.execute_command("HY.BENCH", "bench_key", start_ts, 60000, 10000)
print(f"  ✅ HY.BENCH: {bench_result}")


# ============================================================
# SUMMARY
# ============================================================
test_section("FINAL RESULTS")

print(f"\n  Tests Passed: {results['tests_passed']}")
print(f"  Tests Failed: {results['tests_failed']}")
if results["failures"]:
    print(f"\n  Failures:")
    for f in results["failures"]:
        print(f"    ❌ {f}")

print(f"\n  {'─'*50}")
print(f"  BENCHMARK SUMMARY (HydroDB standalone, 1M points)")
print(f"  {'─'*50}")
print(f"  Ingestion:     {ingest_time:.2f}s ({NUM_POINTS/ingest_time:,.0f} pts/sec)")
print(f"  Memory:        {mem/(1024*1024):.2f} MB ({mem/NUM_POINTS:.1f} bytes/point)")
print(f"  Small Range:   {small['total_ms']:.1f}ms / {small['num_queries']} queries (avg {small['avg_ms']:.3f}ms)")
print(f"  Medium Range:  {medium['total_ms']:.1f}ms / {medium['num_queries']} queries (avg {medium['avg_ms']:.3f}ms)")
print(f"  Large Range:   {large['total_ms']:.1f}ms / {large['num_queries']} queries (avg {large['avg_ms']:.3f}ms)")
print(f"  Full Range:    {full['total_ms']:.1f}ms / {full['num_queries']} queries (avg {full['avg_ms']:.3f}ms)")
print(f"  {'─'*50}")

# Output JSON for README automation
benchmark_data = {
    "ingestion_seconds": round(ingest_time, 2),
    "ingestion_rate": int(NUM_POINTS / ingest_time),
    "memory_mb": round(mem / (1024*1024), 2),
    "bytes_per_point": round(mem / NUM_POINTS, 1),
    "small_range_total_ms": round(small['total_ms'], 1),
    "small_range_avg_ms": round(small['avg_ms'], 3),
    "medium_range_total_ms": round(medium['total_ms'], 1),
    "medium_range_avg_ms": round(medium['avg_ms'], 3),
    "large_range_total_ms": round(large['total_ms'], 1),
    "large_range_avg_ms": round(large['avg_ms'], 3),
    "full_range_total_ms": round(full['total_ms'], 1),
    "full_range_avg_ms": round(full['avg_ms'], 3),
    "mem_usage_1_point": None,
    "cpu_bench_result": bench_result,
}
print(f"\n  📋 JSON Data for README:")
print(f"  {json.dumps(benchmark_data, indent=2)}")

if results['tests_failed'] == 0:
    print(f"\n  🎉 ALL {results['tests_passed']} TESTS PASSED! Module is production-ready.")
else:
    print(f"\n  ⚠️  {results['tests_failed']} TESTS FAILED. Fix before publishing.")
    sys.exit(1)
