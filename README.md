<div align="center">
  <h1>🌊 HydroDB</h1>
  <p><strong>An Ultra-Fast, Uncompressed Time-Series Module for Redis</strong></p>
  <p><i>Trading memory density for absolute, uncompromised CPU speed.</i></p>
</div>

---

## 📌 What is HydroDB?

HydroDB is a custom native Redis Module designed for specific Time-Series use cases where **query speed** is prioritized over **memory footprint**. 

While the official `RedisTimeSeries` module uses Gorilla Compression to pack data incredibly tightly into memory (resulting in slower `O(N)` decompression overhead during queries), **HydroDB stores data completely uncompressed** in "Fluid Buckets" (a hybrid of B-Trees and Unrolled Linked Lists). 

### ⚖️ The Honest Trade-Off

We believe in engineering transparency. HydroDB is not a magic bullet that beats RedisTimeSeries in every aspect. 

* **The Cost (RAM):** HydroDB consumes **~8x to 10x more RAM** than RedisTimeSeries. A 16-byte fixed struct (`uint64_t timestamp`, `double value`) is stored for every data point.
* **The Benefit (Speed):** By keeping data uncompressed in contiguous arrays, HydroDB can perform exact-boundary matching and aggregations in `O(log N)` time. Internally, this algorithmic approach executes up to **250x faster** than sequentially decompressing Gorilla chunks.

If you have abundant RAM and need instantaneous aggregation queries on massive ranges, HydroDB is built for you.

---

## ⚡ Architecture Overview

1. **Pure C Engine**: The core engine (`hydrods_engine.c`) is written in highly-optimized C, bypassing C++ runtime overhead. It leverages `malloc`/`realloc` and POSIX `memmove` to maintain sorted continuous arrays efficiently.
2. **Instant Aggregations**: Mathematical operations (Count, Sum, Min, Max) are executed instantly in RAM over ranges.
3. **Data Durability**: Fully supports Redis native persistence. Data is persisted via RDB Snapshotting and AOF (Append-Only File) rewriting natively.

---

## 🛠️ Build & Installation

To compile the module from source:

```bash
git clone https://github.com/yourusername/hydrodb.git
cd hydrodb/hydro_module
make clean && make
```

Load it into your Redis Server (v6.0+):

```bash
redis-server --loadmodule ./hydro_module/hydrodb.so
```

---

## 📖 Command Reference

### `HY.ADD`
Inserts a single data point into the time series.
* **Syntax:** `HY.ADD <key> <timestamp> <value>`
* **Example:** `HY.ADD CPU_TEMP 1700000000 65.5`

### `HY.MADD`
Atomically inserts multiple data points, heavily reducing TCP latency and parsing overhead.
* **Syntax:** `HY.MADD <key> <timestamp1> <value1> [timestamp2 value2 ...]`
* **Example:** `HY.MADD CPU_TEMP 1700000000 65.5 1700000060 66.1`

### `HY.RANGE`
Queries a time range and optionally aggregates the result in `O(log N)` algorithmic time.
* **Syntax:** `HY.RANGE <key> <start_timestamp> <end_timestamp> [AGGREGATION count|sum|min|max]`
* **Examples:**
  * `HY.RANGE CPU_TEMP 1700000000 1700003600` *(Returns raw element count by default)*
  * `HY.RANGE CPU_TEMP 1700000000 1700003600 AGGREGATION sum`
  * `HY.RANGE CPU_TEMP 1700000000 1700003600 AGGREGATION min`

### `HY.BENCH` (Internal Testing)
An internal benchmarking command used to test raw algorithm CPU execution time independently of the TCP/RESP stack.
* **Syntax:** `HY.BENCH <key> <start_timestamp> <range_duration_seconds> <queries>`

---

## 📊 Performance Benchmarks

When tested over a standard TCP connection using a Python client pipelining **1 Million data points**, here is how HydroDB compares to the official RedisTimeSeries:

| Test Phase (1M Pts over TCP) | Redis TimeSeries | HydroDB | Result |
| :--- | :--- | :--- | :--- |
| **Ingestion** (1M TCP inserts) | `11.4 s` | `11.9 s` | *Gated by TCP/RESP limits* |
| **Small Range** (1440 pts x 500) | `153 ms` | `97 ms` | 🚀 **1.5x Faster** |
| **Medium Range** (43k pts x 500)| `510 ms` | `145 ms` | 🚀 **3.5x Faster** |
| **Large Range** (100k pts x 150)| `255 ms` | `55 ms` | 🚀 **4.6x Faster** |

> **Amdahl's Law in Action:** Over the network, the maximum achievable speedup is heavily bottlenecked by standard TCP connection latency and Redis's RESP string-parsing overhead (adding ~143ms to both module's query times). However, when isolating the core CPU algorithms via internal benchmarking, the HydroDB C-algorithm executes up to **250x faster** than Gorilla-compressed reads.

---

## 🤝 Contributing
Contributions are extremely welcome! We are looking to implement:
- Range Extraction (returning actual arrays of values instead of just aggregations).
- Retention Policies (TTL-based data culling).

Feel free to open an issue or submit a pull request!
