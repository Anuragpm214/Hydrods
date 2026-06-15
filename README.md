<div align="center">
  <h1>🌊 HydroDB</h1>
  <p><strong>An Uncompressed Time-Series Engine for Redis — Built on Fluid Pressure Buckets</strong></p>
  <p><em>A deliberate engineering trade-off: spend memory to eliminate CPU decompression overhead entirely.</em></p>
  <br/>
  <p>
    <img src="https://img.shields.io/badge/Language-C99-blue" alt="C99"/>
    <img src="https://img.shields.io/badge/Redis-Module_API_v1-red" alt="Redis Module"/>
    <img src="https://img.shields.io/badge/License-MIT-green" alt="MIT"/>
    <img src="https://img.shields.io/badge/Status-Experimental-orange" alt="Experimental"/>
  </p>
</div>

---

## Table of Contents
1. [What Problem Does HydroDB Solve?](#-what-problem-does-hydrodb-solve)
2. [The Honest Trade-Off](#%EF%B8%8F-the-honest-trade-off--this-is-not-a-magic-bullet)
3. [Architecture Deep-Dive: Fluid Pressure Buckets](#-architecture-deep-dive-fluid-pressure-buckets)
4. [Algorithmic Complexity Proof](#-algorithmic-complexity-proof)
5. [Performance Benchmarks & Methodology](#-performance-benchmarks--methodology)
6. [Why the Speedup is Capped at ~4.6x Over TCP (Amdahl's Law)](#-why-the-speedup-is-capped-at-46x-over-tcp-amdahls-law)
7. [Build & Installation](#%EF%B8%8F-build--installation)
8. [Command Reference](#-command-reference)
9. [Memory Accounting](#-memory-accounting)
10. [Feature Comparison with RedisTimeSeries](#-feature-comparison-with-redistimeseries)
11. [Current Limitations](#-current-limitations--roadmap)
12. [Contributing](#-contributing)

---

## 📌 What Problem Does HydroDB Solve?

Most time-series databases — including Redis's own [RedisTimeSeries](https://github.com/RedisTimeSeries/RedisTimeSeries) — use **Gorilla Compression** (Facebook, 2015) to pack data tightly. This is excellent for storage density, but introduces an unavoidable cost at query time:

> **To read any data point from a Gorilla-compressed chunk, you must sequentially decompress all preceding points in that chunk from the beginning.** There is no random access.

This means that range queries over compressed chunks have a **hidden `O(N)` decompression overhead**, where `N` is the number of data points in the chunk — not just the points you requested.

**HydroDB takes the opposite approach:** it stores every data point as a raw, uncompressed 16-byte struct (`uint64_t timestamp` + `double value`) inside sorted, contiguous arrays called **Fluid Buckets**. Because the data is uncompressed and sorted, both boundary-finding and aggregation are served directly from memory without any decompression step.

**This means:** if you need 10,000 points from a range of 1 million, HydroDB binary-searches to the exact start and end, then iterates raw memory. RedisTimeSeries must decompress entire chunks to find those same points.

---

## ⚖️ The Honest Trade-Off — This is Not a Magic Bullet

We believe in engineering transparency. HydroDB is **not** a general-purpose replacement for RedisTimeSeries. It is a specialized tool for a specific niche.

### What You Pay (Memory)

Every data point is stored as a raw **16-byte struct**:

```
┌──────────────────────────────────────────┐
│  uint64_t timestamp  │   double value    │  = 16 bytes per point (data only)
│      (8 bytes)       │    (8 bytes)      │
└──────────────────────────────────────────┘
```

However, the **real measured memory usage is ~64 bytes per point**, not 16. The additional ~48 bytes come from:
- **Bucket pre-allocation**: each bucket's internal array is allocated with capacity headroom (~2x) for future inserts
- **Bucket metadata**: 32 bytes per bucket (size, capacity, max_ts, data pointer)
- **Bucket pointer array**: 8 bytes per bucket in the top-level structure
- **Redis allocator overhead**: jemalloc alignment and bookkeeping per allocation

> **Real measurement** (from our test suite, 1M data points in a single key):
> `MEMORY USAGE` reported **61.05 MB** → **64.0 bytes/point**

In comparison, Gorilla compression (used by RedisTimeSeries) achieves ~1.37 bits per value on average (source: Facebook's Gorilla paper, §4.1), translating to roughly **~2 bytes per point**.

| Data Scale | HydroDB RAM (measured) | RedisTimeSeries RAM (est.) | Ratio |
|:-----------|:-----------------------|:---------------------------|:------|
| 10,000 points | ~579 KB *(measured)* | ~20 KB | **~29x** |
| 100,000 points | ~6.06 MB *(measured)* | ~200 KB | **~31x** |
| 1,000,000 points | ~61 MB *(measured)* | ~2 MB | **~31x** |
| 10,000,000 points | ~610 MB *(projected)* | ~20 MB | **~31x** |
| 100,000,000 points | ~6.1 GB *(projected)* | ~200 MB | **~31x** |

> **Honesty note:** The theoretical minimum is 16 bytes/point, but real-world usage including allocator overhead and bucket pre-allocation is **~4x higher**. We report the measured number, not the theoretical one. The ratio vs RedisTimeSeries is **~31x**, not ~8x as a naive calculation would suggest.

### What You Get (Speed)

By eliminating decompression entirely, HydroDB serves range queries and aggregations with **zero CPU overhead beyond traversal**. The larger the query range, the greater the speedup — because the decompression cost in Gorilla grows linearly with range size, while HydroDB's binary-search cost remains logarithmic.

### When to Use HydroDB

✅ You have **abundant RAM** (budget ~64 bytes/point including allocator overhead)  
✅ You need **sub-millisecond aggregation** over large ranges (100K+ points)  
✅ Your workload is **read-heavy** with aggregation queries (monitoring dashboards, alerting engines)  
✅ You're building **real-time analytics** where query latency matters more than storage cost

### When NOT to Use HydroDB

❌ Memory is constrained — use RedisTimeSeries instead  
❌ You need labels, filtering, or multi-key queries (`TS.MRANGE`) — not supported yet  
❌ You need retention policies or automatic downsampling — not supported yet  
❌ Your data has very high cardinality with many sparse keys  

---

## 🧠 Architecture Deep-Dive: Fluid Pressure Buckets

HydroDB's core data structure is an original design we call **Fluid Pressure Buckets** — a hybrid of [B-Trees](https://en.wikipedia.org/wiki/B-tree) and [Unrolled Linked Lists](https://en.wikipedia.org/wiki/Unrolled_linked_list) with a physics-inspired rebalancing algorithm.

### Structure

```
                    ┌─────────────────────────────┐
                    │         hydro_ds             │
                    │  bucket_capacity_limit: 1000 │
                    │  num_buckets: 4              │
                    │  total_elements: 3200        │
                    │  eps_low: 0.50               │
                    │  eps_high: 0.85              │
                    └──────────┬──────────────────-┘
                               │
            ┌──────────────────┼──────────────────────────┐
            │                  │                          │
     ┌──────▼──────┐   ┌──────▼──────┐           ┌──────▼──────┐
     │  Bucket 0   │   │  Bucket 1   │    ...    │  Bucket 3   │
     │  size: 800  │   │  size: 850  │           │  size: 750  │
     │  max_ts: T₀ │   │  max_ts: T₁ │           │  max_ts: T₃ │
     └──────┬──────┘   └──────┬──────┘           └──────┬──────┘
            │                  │                          │
   ┌────────▼────────┐ ┌──────▼────────┐        ┌────────▼────────┐
   │ [ts,val][ts,val] │ │ [ts,val]      │        │ [ts,val][ts,val] │
   │ [ts,val][ts,val] │ │ [ts,val]      │  ...   │ [ts,val]         │
   │ ... (contiguous) │ │ ...(sorted)   │        │ ... (contiguous) │
   └──────────────────┘ └───────────────┘        └──────────────────┘
        Sorted C array      Sorted C array            Sorted C array
```

**Key properties:**
- Each bucket is a **contiguous C array** of `hydro_ts_node` structs (cache-line friendly)
- Buckets are **globally sorted** — all timestamps in Bucket `i` are ≤ all timestamps in Bucket `i+1`
- The `max_ts` index on each bucket enables **O(log B) bucket-level binary search**
- Within each bucket, data is sorted, enabling **O(log K) element-level binary search**

### The Fluid Pressure Rebalancing Algorithm

Instead of traditional B-Tree splits that always produce two half-full nodes, HydroDB uses a physics-inspired **pressure equalization** model:

**Pressure** of a bucket is defined as:

```
P(bucket) = bucket.size / bucket_capacity_limit
```

When a bucket's pressure exceeds the high threshold (`eps_high = 0.85`), elements **"flow"** from the high-pressure bucket to adjacent lower-pressure buckets — exactly like fluid equalizing between connected chambers.

```
Before Flow:                          After Flow:
┌────────────┐  ┌────────────┐       ┌────────────┐  ┌────────────┐
│ ████████░░ │  │ ███░░░░░░░ │       │ ██████░░░░ │  │ ██████░░░░ │
│ P = 0.90   │→ │ P = 0.30   │  ──►  │ P = 0.60   │  │ P = 0.60   │
└────────────┘  └────────────┘       └────────────┘  └────────────┘
  High pressure   Low pressure         Equalized       Equalized
```

**The flow amount** `k` is calculated as:

```
k = bucket_capacity_limit × (ΔP - eps_low) / 2
```

Where `ΔP = P(source) - P(target)`. This formula ensures:
- Flow is **proportional** to the pressure differential (bigger imbalance → more flow)
- Flow **stops** when pressure difference drops below `eps_low` (avoids thrashing)
- `eps_low = 0.50` and `eps_high = 0.85` provide a hysteresis band that prevents oscillation

**When flow isn't enough** (bucket exceeds `bucket_capacity_limit`), a traditional **split** occurs — dividing the bucket at its midpoint and inserting a new bucket into the array.

### Why This Design is Cache-Friendly

Each bucket's data lives in a single `malloc`'d contiguous array. When the CPU scans a bucket during a range query, it benefits from:

1. **Spatial locality** — consecutive data points are adjacent in memory
2. **Hardware prefetching** — CPU prefetchers detect sequential access patterns in contiguous arrays
3. **No pointer chasing** — unlike linked lists or tree nodes, there are no pointers to dereference within a bucket

This is the same principle that makes arrays faster than linked lists for sequential access in practice, despite both being `O(N)` theoretically.

---

## 📐 Algorithmic Complexity Proof

Let `N` = total data points, `B` = number of buckets, `K` = average bucket size (≈ `N/B`).

### Insert: `O(log B + K)`

1. **Find target bucket:** Binary search over `max_ts` index → `O(log B)`
2. **Find insert position within bucket:** Binary search → `O(log K)`
3. **Shift elements via `memmove`:** Worst case `O(K)`, but `memmove` is a single optimized CPU instruction on modern hardware
4. **Stabilize (flow/split):** Amortized `O(1)` — splits are rare and flow moves bounded elements

### Range Aggregation: `O(log B + log K + R)`

Where `R` = number of data points in the result range.

1. **Find starting bucket:** Binary search over `max_ts` → `O(log B)`
2. **Find start position within first bucket:** `lower_bound` binary search → `O(log K)`
3. **Iterate matching elements across buckets:** `O(R)` — raw sequential memory scan, no decompression

### Comparison with Gorilla Compression (RedisTimeSeries)

| Operation | HydroDB | Gorilla (RedisTimeSeries) |
|:----------|:--------|:--------------------------|
| Point Insert | `O(log B + K)` | `O(1)` amortized append |
| Range Query (R results) | `O(log B + log K + R)` | `O(C)` where C = chunk size (must decompress full chunk) |
| Aggregation over range | `O(log B + log K + R)` | `O(C)` per chunk touched |
| Random Access (single point) | `O(log B + log K)` | `O(C)` (no random access in Gorilla) |

> **Key insight:** Gorilla's query cost is proportional to the **chunk size** `C` (typically 4096+ samples), not the result size `R`. If you request 10 points from a chunk of 4096, Gorilla still decompresses all 4096. HydroDB binary-searches to the exact 10 points.

---

## 📊 Performance Benchmarks & Methodology

### Test Environment

- **Redis:** v7.0.15 with HydroDB module loaded
- **OS:** Linux (Ubuntu)
- **Data:** 1,000,000 data points in a single key, 1-minute intervals, value = 100.0 ± random uniform jitter
- **Client:** Python `redis-py` (v8.0.0) with pipelining (batch size: 10,000)
- **Connection:** Standard TCP loopback (`127.0.0.1:6387`), RESP2 protocol
- **Methodology:** Range queries use **randomized start positions** to avoid caching bias. All numbers are from a single run, not cherry-picked.

### Reproducibility

The complete, reproducible benchmark script is in [`hydro_module/full_test_suite.py`](hydro_module/full_test_suite.py). Run it yourself:

```bash
redis-server --port 6387 --loadmodule /path/to/hydrodb.so --daemonize yes
cd hydro_module && python3 full_test_suite.py
```

### HydroDB Standalone Results (1M Points, Real Measured Data)

| Test Phase | Range Size | Queries | Total Time | Avg/Query | p50 | p99 |
|:-----------|:-----------|:--------|:-----------|:----------|:----|:----|
| **Ingestion** | 1M inserts | — | `12.85 s` | — | — | — |
| **Small Range** | 1,440 pts | 500 | `108.3 ms` | `0.217 ms` | `0.168 ms` | `0.683 ms` |
| **Medium Range** | 43,200 pts | 500 | `277.0 ms` | `0.554 ms` | `0.542 ms` | `1.002 ms` |
| **Large Range** | 100,000 pts | 150 | `131.9 ms` | `0.879 ms` | `0.901 ms` | `1.203 ms` |
| **Full Range** | 1,000,000 pts | 50 | `234.4 ms` | `4.687 ms` | `4.725 ms` | `5.785 ms` |

**Ingestion rate:** ~77,844 inserts/sec over TCP pipeline.

**Aggregation breakdown** (full 1M range, avg per query):

| Aggregation | Avg Time |
|:------------|:---------|
| COUNT | `4.794 ms` |
| SUM | `4.993 ms` |
| MIN | `4.552 ms` |
| MAX | `4.717 ms` |

> **Note on ingestion speed:** HydroDB's insert is `O(log B + K)` due to sorted insertion with `memmove`, while RedisTimeSeries appends in `O(1)` amortized (Gorilla encoding is append-only). Over TCP, both are dominated by network RTT and RESP parsing overhead. In a CPU-only microbenchmark, RedisTimeSeries ingestion would be faster.

> **Note on range queries:** These numbers are HydroDB-only. For a head-to-head comparison with RedisTimeSeries, use the separate [`module_benchmark.py`](hydro_module/module_benchmark.py) script with both modules loaded. In previous runs, HydroDB showed 1.5x–4.6x speedups depending on range size.

---

## 📏 Why Network Speedup is Smaller Than Algorithmic Speedup (Amdahl's Law)

You might wonder: if HydroDB's CPU algorithm is so much faster, why does the network benchmark only show ~1.5x–4.6x speedup? This is not a contradiction — it's [Amdahl's Law](https://en.wikipedia.org/wiki/Amdahl%27s_law).

**Amdahl's Law** states that the maximum speedup of a system is limited by the portion that **cannot** be improved:

```
                        1
Speedup = ─────────────────────────────
           (1 - P) + P / S
```

Where:
- `P` = fraction of total time spent in the "improvable" part (the CPU algorithm)
- `S` = speedup of the improved part
- `(1 - P)` = fraction spent in the "fixed" overhead (TCP round-trip + RESP serialization/parsing)

### Real Measured Proof: CPU-Only vs. Network

Our `HY.BENCH` command runs the range query algorithm **inside the Redis process**, bypassing all TCP/RESP overhead. Here are the real numbers:

```
┌─────────────────────────────────────────────────────────────┐
│  HY.BENCH result (10,000 queries, ~60K pts each):          │
│  "Algorithm Time for 10000 queries: 60.642 ms"             │
│  → ~0.006 ms per query (pure CPU algorithm time)           │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│  Network benchmark (same range size, via TCP):              │
│  Large range (100K pts): avg 0.879 ms per query            │
│  → Of which ~0.006 ms is CPU, ~0.873 ms is TCP/RESP        │
└─────────────────────────────────────────────────────────────┘
```

**Breakdown:**
```
HydroDB query time (single, 100K range, over TCP):
├── TCP Round-Trip + RESP Parse:   ~0.873 ms   (99.3% — fixed overhead)
├── Binary Search + Raw Scan:      ~0.006 ms   (0.7%  — CPU algorithm)
└── Total:                         ~0.879 ms
```

The CPU algorithm is **already near-instant** — it's the TCP/RESP overhead that dominates. Even with an infinitely fast algorithm (S → ∞), the query would still take ~0.873 ms due to network overhead alone.

**This is why we provide `HY.BENCH`** — to prove that the algorithmic advantage is real, even when TCP overhead masks it in network benchmarks. The comparison with RedisTimeSeries's Gorilla decompression is meaningful at the CPU level, where the decompression step adds O(N) overhead that HydroDB completely eliminates.

---

## 🛠️ Build & Installation

### Prerequisites

- GCC (or any C99-compatible compiler)
- Redis Server v6.0 or later
- GNU Make

### Compile

```bash
git clone https://github.com/yourusername/hydrodb.git
cd hydrodb/hydro_module
make clean && make
```

This produces `hydrodb.so` — the loadable Redis module.

### Load into Redis

```bash
# Option 1: Command-line flag
redis-server --loadmodule ./hydro_module/hydrodb.so

# Option 2: redis.conf
# Add this line to your redis.conf:
# loadmodule /absolute/path/to/hydrodb.so
```

### Verify

```bash
redis-cli
127.0.0.1:6379> MODULE LIST
# Should show "hydro" in the list

127.0.0.1:6379> HY.ADD test_key 1700000000 42.5
OK

127.0.0.1:6379> HY.RANGE test_key 0 9999999999 AGGREGATION count
(integer) 1
```

---

## 📖 Command Reference

### `HY.ADD`

Inserts a single data point into a time-series key. Creates the key if it doesn't exist.

```
HY.ADD <key> <timestamp> <value>
```

- `timestamp` — Non-negative integer (Unix timestamp in seconds, milliseconds, or any monotonic counter)
- `value` — IEEE 754 double-precision floating point

```bash
127.0.0.1:6379> HY.ADD cpu:host1 1700000000 65.5
OK
127.0.0.1:6379> HY.ADD cpu:host1 1700000060 66.1
OK
```

**Error conditions:**
- `ERR invalid timestamp` — timestamp is not a valid integer
- `ERR timestamp must be non-negative` — negative timestamps are rejected
- `ERR invalid value` — value is not a valid float
- `WRONGTYPE` — key exists but is not a HydroDB type

---

### `HY.MADD`

Atomically inserts multiple data points into a single key. **All arguments are validated before any insertion occurs** — if any timestamp or value is invalid, the entire command is rejected with zero side effects.

```
HY.MADD <key> <ts1> <val1> [ts2 val2 ...]
```

```bash
127.0.0.1:6379> HY.MADD cpu:host1 1700000000 65.5 1700000060 66.1 1700000120 64.8
(integer) 3
```

Returns the number of data points inserted.

**Error conditions:**
- `ERR invalid timestamp in MADD arguments` — one or more timestamps are invalid
- `ERR timestamp must be non-negative` — one or more timestamps are negative
- `ERR invalid value in MADD arguments` — one or more values are invalid

---

### `HY.RANGE`

Queries a time range and returns aggregated results. The boundary matching uses binary search (`O(log N)`), and the aggregation scans raw memory (`O(R)` where R = matching points).

```
HY.RANGE <key> <start_ts> <end_ts> [AGGREGATION count|sum|min|max]
```

- Default aggregation (no `AGGREGATION` keyword) returns `count`
- Returns `(nil)` if no points match the range
- Returns an empty array if the key doesn't exist

```bash
# Count points in a 1-hour window
127.0.0.1:6379> HY.RANGE cpu:host1 1700000000 1700003600
(integer) 60

# Sum of all values
127.0.0.1:6379> HY.RANGE cpu:host1 1700000000 1700003600 AGGREGATION sum
"3945.6"

# Min/Max
127.0.0.1:6379> HY.RANGE cpu:host1 1700000000 1700003600 AGGREGATION min
"62.3"
127.0.0.1:6379> HY.RANGE cpu:host1 1700000000 1700003600 AGGREGATION max
"71.8"
```

---

### `HY.BENCH` *(Internal / Diagnostic)*

Runs a CPU-only benchmark **inside** the Redis server process, bypassing all TCP and RESP overhead. This measures the raw algorithmic execution time of the range query engine.

```
HY.BENCH <key> <start_ts> <range_duration_seconds> <num_queries>
```

```bash
127.0.0.1:6379> HY.BENCH bench_key 1700000000 60000 10000
"Algorithm Time for 10000 queries: 60.642 ms (checksum: 582039400)"
# → 0.006 ms per query (pure CPU, no TCP/RESP overhead)
```

> ⚠️ **Warning:** This command executes a tight CPU loop inside the main Redis thread. Use only for diagnostic and benchmarking purposes, not in production workloads.

---

## 💾 Memory Accounting

HydroDB uses `RedisModule_Alloc` / `RedisModule_Realloc` / `RedisModule_Free` for all internal allocations. This means:

- ✅ `INFO memory` correctly reports HydroDB's memory usage
- ✅ `MEMORY USAGE <key>` returns accurate per-key memory consumption
- ✅ Redis `maxmemory` policy correctly accounts for HydroDB data
- ✅ `deny-oom` flag on write commands prevents inserts when memory is full

### Per-Key Memory Formula

```
Memory(key) = sizeof(hydro_ds)                          [56 bytes]
            + num_buckets × sizeof(hydro_bucket)         [32 bytes each]
            + num_buckets × sizeof(hydro_bucket*)        [8 bytes each]
            + total_points × sizeof(hydro_ts_node)       [16 bytes each]
            + pre-allocated_unused_slots                  [bucket capacity headroom]
            + allocator_overhead                          [jemalloc alignment/bookkeeping]
```

### Real Measured Values

| Data Scale | MEMORY USAGE (measured) | Bytes/Point | Notes |
|:-----------|:------------------------|:------------|:------|
| 1 point | 16,144 bytes | 16,144 | Fixed overhead dominates |
| 10,000 points | 592,800 bytes (579 KB) | 59.3 | Overhead amortizing |
| 100,000 points | 6,358,912 bytes (6.06 MB) | 63.6 | Converging to steady-state |
| 1,000,000 points | 64,016,448 bytes (61.05 MB) | 64.0 | Steady-state |

**At scale, memory usage converges to ~64 bytes/point.** The breakdown:
- 16 bytes → actual data struct (`timestamp` + `value`)
- ~16 bytes → bucket pre-allocation headroom (capacity ~2x size)
- ~32 bytes → per-element share of bucket metadata + allocator overhead

> These numbers are from `MEMORY USAGE <key>` on a real Redis 7.0.15 instance with jemalloc. Your numbers may vary slightly depending on allocator and Redis version.

---

## 🔄 Data Durability

HydroDB fully integrates with Redis's native persistence mechanisms:

| Mechanism | Status | Details |
|:----------|:-------|:--------|
| **RDB Snapshots** | ✅ Supported | Custom `rdb_save` / `rdb_load` callbacks serialize all buckets |
| **AOF Persistence** | ✅ Supported | `aof_rewrite` emits `HY.ADD` commands for each data point |
| **Replication** | ✅ Supported | `ReplicateVerbatim` propagates write commands to replicas |
| **`DEL` Command** | ✅ Safe | Custom `free` callback releases all bucket memory without leaks |
| **RDB Version Check** | ✅ Handled | `encver` is validated on load for forward compatibility |

---

## 🔍 Feature Comparison with RedisTimeSeries

| Feature | RedisTimeSeries | HydroDB | Notes |
|:--------|:----------------|:--------|:------|
| Compression | Gorilla (delta-of-delta + XOR) | None (raw 16-byte structs) | Trade-off: memory vs. speed |
| RAM per point (measured) | ~2 bytes | ~64 bytes | **~31x more RAM** (includes allocator overhead) |
| Insert complexity | `O(1)` amortized | `O(log B + K)` | RTS is faster for ingestion |
| Range query complexity | `O(C)` per chunk | `O(log B + log K + R)` | HydroDB is faster for reads |
| Aggregation types | 14+ (avg, std, twa, etc.) | 4 (count, sum, min, max) | **Gap** |
| Labels & filtering | ✅ `TS.MRANGE` with filters | ❌ Not supported | **Gap** |
| Retention policies | ✅ `RETENTION <ms>` | ❌ Not supported | **Gap** |
| Downsampling rules | ✅ `TS.CREATERULE` | ❌ Not supported | **Gap** |
| Raw data extraction | ✅ Returns `[ts, val]` pairs | ❌ Aggregation only | **Gap — in progress** |
| Duplicate policy | ✅ Configurable | ❌ Allows duplicates | **Gap** |
| RDB persistence | ✅ | ✅ | Parity |
| AOF persistence | ✅ | ✅ | Parity |
| Memory tracking | ✅ `RedisModule_Alloc` | ✅ `RedisModule_Alloc` | Parity |
| Redis `MEMORY USAGE` | ✅ | ✅ | Parity |

---

## 🚧 Current Limitations & Roadmap

We believe in listing limitations explicitly rather than hiding them.

### Known Limitations

| Limitation | Impact | Planned? |
|:-----------|:-------|:---------|
| No raw data point extraction (`HY.RANGE` returns aggregations only) | Cannot retrieve actual `(timestamp, value)` pairs | ✅ In progress |
| No retention policies / TTL | Memory grows unbounded without manual `DEL` | ✅ Planned |
| No downsampling / compaction rules | No automatic rollup of old data | 🔜 Planned |
| No labels or multi-key filtering | Cannot do `TS.MRANGE`-style queries | 🔜 Planned |
| Only 4 aggregation types | Missing `avg`, `std`, `first`, `last`, `range`, `twa` | ✅ Planned |
| Duplicate timestamps not rejected | Same timestamp can be inserted twice | ✅ Planned |
| Single-threaded (Redis main thread) | Concurrent queries are serialized by Redis | By design (Redis module model) |

### Roadmap

- [ ] **v0.2** — Raw data extraction in `HY.RANGE` (return `[ts, val]` arrays)
- [ ] **v0.2** — Additional aggregations (`avg`, `first`, `last`)
- [ ] **v0.3** — Retention policies (`HY.ADD key ts val RETENTION 86400000`)
- [ ] **v0.3** — Duplicate timestamp policies (`ON_DUPLICATE LAST|SUM|MIN|MAX`)
- [ ] **v0.4** — Keyspace notifications for monitoring integration
- [ ] **v0.4** — `INFO` section with engine statistics

---

## 🧪 Testing

A comprehensive test suite is included:

```bash
# Start Redis with HydroDB loaded
redis-server --loadmodule ./hydro_module/hydrodb.so --port 6387

# Run tests (requires redis-py)
cd hydro_module
python3 test_module.py
```

**Test coverage:**

| # | Test | What It Validates |
|:--|:-----|:------------------|
| 1 | Basic `HY.ADD` | Single insert + range count |
| 2 | `HY.MADD` | Bulk insert correctness |
| 3 | Aggregations | `sum`, `min`, `max` mathematical correctness |
| 4 | Edge: start > end | Graceful handling of invalid ranges |
| 5 | Edge: missing key | Returns empty array (no crash) |
| 6 | Invalid aggregation | Returns proper error message |
| 7 | `DEL` memory release | No segfault after key deletion |
| 8 | RDB persistence | `BGSAVE` completes without error |

---

## 🤝 Contributing

Contributions are welcome. If you're interested in contributing, here are the highest-impact areas:

1. **Raw data extraction** — Making `HY.RANGE` return actual `(timestamp, value)` arrays
2. **More aggregation types** — `avg`, `first`, `last`, `std.p`, `range`
3. **Retention policies** — TTL-based automatic data culling
4. **Duplicate timestamp handling** — Configurable policy on insert conflicts

Please open an issue to discuss before submitting large PRs.

---

## 📄 License

MIT License. See [LICENSE](LICENSE) for details.

---

<div align="center">
  <sub>Built with ☕ and obsessive performance profiling.</sub>
</div>
