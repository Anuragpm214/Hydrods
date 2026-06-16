<div align="center">

# 🌊 HydroDB

### A Blazing-Fast NoSQL Database Engine Built From Scratch in C++17

*No Redis. No dependencies. Just raw performance.*

<br/>

[![C++17](https://img.shields.io/badge/C++-17-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/17)
[![Linux](https://img.shields.io/badge/Linux-Only-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://kernel.org)
[![Protocol](https://img.shields.io/badge/Protocol-RESP-DC382D?style=for-the-badge&logo=redis&logoColor=white)](https://redis.io/docs/reference/protocol-spec/)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)
[![Python SDK](https://img.shields.io/badge/SDK-Python-3776AB?style=for-the-badge&logo=python&logoColor=white)](hydrods-python/)

<br/>

**⚡ Epoll Event Loop** · **🔒 Thread-Safe RW Locks** · **💾 AOF Persistence** · **🐍 Python SDK**

</div>

---

## 🤔 What is HydroDB?

HydroDB is a **standalone, high-performance key-value database** written entirely in C++17. It doesn't depend on Redis or any external database — it IS the database.

Under the hood, it uses a novel data structure called **Fluid Pressure Buckets** — imagine a B-tree that never does expensive rebalancing. Instead, data "flows" between contiguous memory buckets like water equalizing pressure, giving you insane cache locality and CPU prefetcher optimization.

> **TL;DR** — It's fast, it's simple, and you can talk to it with `redis-cli` or the bundled Python SDK.

---

## ✨ Key Features

| Feature | Description |
|:--------|:------------|
| 🧠 **Fluid Pressure Buckets** | Custom data structure — cache-friendly sorted vectors that auto-balance without tree rotations |
| ⚡ **Epoll Event Loop** | Non-blocking async I/O with `EPOLLOUT` write buffering — the event loop never stalls |
| 🔒 **Thread-Safe** | `std::shared_mutex` with atomic writer-tracking prevents read starvation under heavy writes |
| 📡 **RESP Protocol** | Speaks native Redis protocol — use `redis-cli`, `redis-py`, or any Redis client |
| 💾 **AOF Persistence** | Every write is asynchronously logged to an Append-Only File for crash recovery |
| 🐍 **Python SDK** | Lightweight native client — no `redis-py` dependency needed |
| 📊 **O(1) Stats** | `DBSIZE`, `HMIN`, `HMAX` read directly from bucket boundaries — instant answers |

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────┐
│                   Client Layer                      │
│         redis-cli  ·  Python SDK  ·  Any RESP Client│
└──────────────────────┬──────────────────────────────┘
                       │ TCP (port 7379)
                       ▼
┌─────────────────────────────────────────────────────┐
│              Epoll Event Loop (server.cpp)          │
│  ┌─────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │ Accept  │  │ RESP Parser  │  │ EPOLLOUT      │  │
│  │ Handler │  │ & Router     │  │ Write Buffer  │  │
│  └─────────┘  └──────┬───────┘  └───────────────┘  │
└──────────────────────┼──────────────────────────────┘
                       ▼
┌─────────────────────────────────────────────────────┐
│           HydroDB Engine (hydrodb_engine.hpp)       │
│  ┌───────────────┐  ┌────────────┐  ┌───────────┐  │
│  │ shared_mutex  │  │ SET/GET/   │  │ Range     │  │
│  │ RW Locking    │  │ DEL Logic  │  │ Queries   │  │
│  └───────────────┘  └────────────┘  └───────────┘  │
└──────────────────────┼──────────────────────────────┘
                       ▼
┌─────────────────────────────────────────────────────┐
│          Fluid Pressure Buckets (hydrods.hpp)       │
│                                                     │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐   │
│  │Bucket 0│◄►│Bucket 1│◄►│Bucket 2│◄►│Bucket N│   │
│  │ sorted │  │ sorted │  │ sorted │  │ sorted │   │
│  │ vector │  │ vector │  │ vector │  │ vector │   │
│  └────────┘  └────────┘  └────────┘  └────────┘   │
│       ↕ flow()    ↕ flow()    ↕ flow()             │
│                                                     │
│  Elements "flow" between buckets to maintain        │
│  balanced pressure — no tree rotations needed!      │
└─────────────────────────────────────────────────────┘
                       ▼
┌─────────────────────────────────────────────────────┐
│         AOF Persistence (Background Thread)         │
│         Async append to hydrodb.aof file            │
└─────────────────────────────────────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites

- **GCC** or **Clang** with C++17 support
- **GNU Make**
- **Linux** (requires `epoll`)

### Build & Run

```bash
# Clone the repository
git clone https://github.com/Anuragpm214/Hydrods.git
cd Hydrods

# Build
make clean && make

# Run the server
./hydrodb_server
```

You'll see:
```
==============================================
  🌊 HydroDB Server v1.1 (Optimized)        
  Listening on TCP port 7379...               
  EPOLL Event Loop:   ENABLED                
  EPOLLOUT Buffering: ENABLED                
  AOF Persistence:    ENABLED                
  Commands: SET GET DEL HRANGE PING QUIT     
            DBSIZE HMIN HMAX                 
==============================================
```

### Try it Out

```bash
# Connect with redis-cli (yes, it just works!)
redis-cli -p 7379
```

```
127.0.0.1:7379> SET user:101 "Anurag Panwar"
+OK

127.0.0.1:7379> GET user:101
"Anurag Panwar"

127.0.0.1:7379> SET user:102 "John Doe"
+OK

127.0.0.1:7379> HRANGE user:000 user:999
1) "user:101"
2) "Anurag Panwar"
3) "user:102"
4) "John Doe"

127.0.0.1:7379> DBSIZE
(integer) 2

127.0.0.1:7379> HMIN
1) "user:101"
2) "Anurag Panwar"

127.0.0.1:7379> HMAX
1) "user:102"
2) "John Doe"

127.0.0.1:7379> DEL user:102
(integer) 1
```

---

## 📖 Command Reference

| Command | Syntax | Time Complexity | Description |
|:--------|:-------|:----------------|:------------|
| **SET** | `SET <key> <value>` | `O(log B + log K)` | Insert or update a key-value pair |
| **GET** | `GET <key>` | `O(log B + log K)` | Retrieve value by key |
| **DEL** | `DEL <key>` | `O(log B + log K)` | Delete a key-value pair |
| **HRANGE** | `HRANGE <start> <end> [LIMIT offset count]` | `O(log B + R)` | Lexicographic range query with optional pagination |
| **DBSIZE** | `DBSIZE` | `O(1)` | Total number of keys stored |
| **HMIN** | `HMIN` | `O(1)` | Smallest key-value pair (lexicographic) |
| **HMAX** | `HMAX` | `O(1)` | Largest key-value pair (lexicographic) |
| **PING** | `PING` | `O(1)` | Health check → returns `PONG` |
| **QUIT** | `QUIT` | `O(1)` | Gracefully close client connection |

> **B** = number of buckets · **K** = elements per bucket · **R** = result set size

---

## 🐍 Python SDK

HydroDB ships with a **zero-dependency Python client** — no need to install `redis-py`.

### Install

```bash
cd hydrods-python
pip install .
```

### Usage

```python
from hydrods import HydroDB

# Connect
db = HydroDB(host='127.0.0.1', port=7379)

# Basic CRUD
db.set("sensor:001", "temp: 22.5")
db.set("sensor:002", "temp: 23.1")
db.set("sensor:003", "temp: 21.8")

print(db.get("sensor:001"))       # → "temp: 22.5"
print(db.delete("sensor:003"))    # → True

# Range Query — get all sensors in one call
results = db.range("sensor:000", "sensor:999")
for key, value in results.items():
    print(f"  {key} → {value}")

# Pagination support
page = db.range("sensor:000", "sensor:999", limit=10, offset=0)

# Cleanup
db.close()
```

### SDK Methods

| Method | Returns | Description |
|:-------|:--------|:------------|
| `set(key, value)` | `bool` | Set a key-value pair |
| `get(key)` | `str \| None` | Get value by key |
| `delete(key)` | `bool` | Delete a key |
| `range(start, end, limit, offset)` | `dict` | Lexicographic range query |
| `close()` | — | Close the connection |

---

## 🧠 How Fluid Pressure Buckets Work

Traditional databases use **B-Trees** or **Skip Lists** that fragment memory across scattered heap nodes — causing cache misses on every lookup.

HydroDB takes a different approach:

```
Traditional B-Tree:                    Fluid Pressure Buckets:
                                       
  [scattered heap nodes]               [contiguous memory vectors]
       ↗     ↘                         ┌──────┬──────┬──────┐
   [node]   [node]                     │ vec0 │ vec1 │ vec2 │
    ↗ ↘      ↗ ↘                       │sorted│sorted│sorted│
 [n] [n]  [n]  [n]                    └──────┴──────┴──────┘
                                            ←flow→
 ❌ Random memory jumps                ✅ Sequential memory access
 ❌ Cache misses everywhere            ✅ CPU prefetcher loves this
 ❌ Expensive rotations/splits         ✅ Elements "flow" naturally
```

**How the flow works:**

1. Each bucket is a sorted `std::vector` with a target capacity `C`
2. When a bucket's **pressure** (`size / C`) exceeds the high threshold → elements flow to neighbors
3. When pressure difference is too large → elements redistribute like water finding its level
4. If a bucket overflows beyond `C` → it splits into two (rare operation)

This gives you:
- **O(log B)** bucket lookup via binary search on bucket maximums
- **O(log K)** element lookup within a bucket via `std::lower_bound`
- **Near-zero cache misses** thanks to contiguous vector memory

---

## 📁 Project Structure

```
Hydrods/
├── src/
│   └── server.cpp              # Epoll event loop + RESP parser + command router
├── include/
│   ├── hydrodb_engine.hpp      # Thread-safe DB engine (SET/GET/DEL/RANGE + RW locks)
│   └── hydrods.hpp             # Core Fluid Pressure Buckets data structure
├── hydrods-python/
│   ├── hydrods/
│   │   ├── __init__.py
│   │   └── client.py           # Python SDK client
│   ├── setup.py
│   └── README.md
├── Makefile                    # Build configuration
└── README.md                   # ← You are here
```

---

## 🛡️ Technical Highlights

<details>
<summary><b>🔒 Thread Safety — Writer Starvation Prevention</b></summary>

HydroDB uses `std::shared_mutex` for concurrent reads with exclusive writes. But there's a twist: an atomic `pending_writers` counter ensures readers **yield** when a writer is waiting — preventing write starvation under heavy read loads.

```cpp
// Readers check for pending writers before acquiring shared lock
if (pending_writers.load(std::memory_order_acquire) > 0) {
    // Wait until writers finish
    pending_cv.wait(plock, [this] { return pending_writers == 0; });
}
std::shared_lock lock(rw_lock);  // Then proceed with read
```
</details>

<details>
<summary><b>⚡ EPOLLOUT — Non-Blocking Write Buffering</b></summary>

Large range query responses can exceed the kernel's TCP send buffer. Instead of blocking, HydroDB buffers the response internally and registers an `EPOLLOUT` event. The event loop resumes writing exactly when the kernel is ready.

```
Client sends HRANGE → Server processes → Response too large for TCP buffer?
    ├─ NO  → Write directly, done ✅
    └─ YES → Buffer response → Register EPOLLOUT → Event loop resumes later ✅
```
</details>

<details>
<summary><b>💾 Async AOF Persistence</b></summary>

Every mutation (SET/DEL) is pushed to a lock-free queue. A dedicated background thread batch-flushes these to `hydrodb.aof`. On restart, the AOF is replayed to restore full state — zero data loss.

</details>

---

## 🤝 Contributing

Contributions are welcome! Here's how:

1. **Fork** the repository
2. **Create** a feature branch: `git checkout -b feature/amazing-feature`
3. **Commit** your changes: `git commit -m "Add amazing feature"`
4. **Push** to the branch: `git push origin feature/amazing-feature`
5. **Open** a Pull Request

---

## 📜 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

<div align="center">

**Built with ❤️ and C++ by [Anurag Panwar](https://github.com/Anuragpm214)**

*If you found this useful, consider giving it a ⭐!*

</div>
