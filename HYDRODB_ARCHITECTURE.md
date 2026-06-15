# 🌊 HydroDB: Architecture

HydroDB is a high-performance, multi-threaded, NoSQL in-memory database built entirely in C++17. It is designed to act as a thread-safe, concurrent alternative to single-threaded databases like Redis, speaking the same RESP protocol over TCP.

---

## 1. 🧠 Core Data Structure (`include/hydrods.hpp`)
Instead of a standard `std::unordered_map` or a slow Linked List, HydroDB uses a **Custom B-Tree/SkipList Hybrid** called dynamic "Buckets" (`HydroDS`).

*   **Structure:** Data is stored in `std::vector<std::vector<T>> buckets`. Keys inside a bucket are always sorted alphabetically.
*   **Fluid Stabilization (The Hydro-Dynamic part):** `flow(i, j)` and `stabilize(i)` act like water pressure. If one bucket gets too full, the data "flows" into neighboring empty buckets to keep memory perfectly balanced.
*   **Capacity & Splitting:** If a bucket overflows its absolute maximum capacity, it is split in half (`split_bucket(i)`), maintaining `O(log N)` search time.

## 2. 🔐 Concurrency & Threading (`include/hydrodb_engine.hpp`)
To prevent Data Corruption while allowing massive concurrency across modern CPU cores, HydroDB wraps `HydroDS` with a custom Reader-Writer engine:

*   **GET/ZRANGE (Concurrent Reads):** Uses `std::shared_lock lock(rw_lock)`. Infinite threads can read simultaneously across all CPU cores without blocking each other.
*   **SET/DEL (Exclusive Writes):** Uses `std::unique_lock lock(rw_lock)`.
*   **Starvation Prevention:** Linux `pthread_rwlock` defaults to reader-preference, which causes writers to starve under high load. HydroDB implements a custom `WriterTracker` using `std::mutex` and `std::condition_variable` that forces new readers to wait if any writer is queued. This guarantees 100% fairness and zero deadlocks under heavy concurrent access.

## 3. 🌐 Networking & Thread Pool (`src/server.cpp`)
HydroDB speaks raw TCP sockets using the Redis Serialization Protocol (RESP) on port `7379`.

*   **Epoll Event Loop / Redis Native:** Originally built as a standalone non-blocking `epoll` server, it is now integrated directly into Redis as a Native Module. This means it inherits Redis's incredibly fast, single-threaded event loop architecture to manage thousands of TCP connections with zero context-switching overhead.
*   **Pipelining & Resilience:** The server handles pipelined bulk commands and explicitly ignores `SIGPIPE` signals, ensuring that sudden client disconnects or pipeline breaks do not crash the engine.

## 4. 💾 Zero-Latency Disk Persistence (Asynchronous AOF)
RAM is volatile. HydroDB uses an **AOF (Append Only File)** (`hydrodb.aof`) to persist data to the hard disk.

*   **Async Background Writer:** Hard disks are slow. When a `SET` happens, the command is instantly pushed to an in-memory Queue (`aof_queue`), and the client receives a `+OK` response in microseconds.
*   **Dedicated Flusher:** A separate detached background thread wakes up (`aof_cv.wait`), swaps the queue lock-free, and writes the batch to the physical hard disk asynchronously (`aof_file.flush()`).
*   **Boot Recovery:** On startup, the server automatically reads `hydrodb.aof` and replays all commands to completely restore the in-memory state.

---
**Final Conclusion:** By combining Data Structure Engineering (Fluid Buckets), robust Systems Programming (Shared Locks & Thread Pools), and OS features (Async I/O), HydroDB acts as a highly capable, thread-safe, next-generation caching and storage engine.
