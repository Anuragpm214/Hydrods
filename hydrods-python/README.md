# HydroDB Python Client (`hydrods`)

The official Python client library for **HydroDB** - a blazing fast, multi-threaded NoSQL database built in C++17. 

HydroDB is designed to outperform single-threaded databases like Redis in highly concurrent environments by utilizing a unified **Reader-Writer Starvation-Free Lock Engine**, dynamic **Fluid Pressure Buckets**, and Zero-Latency Asynchronous AOF Persistence.

## 🚀 Installation

```bash
pip install hydrods
```

## 💻 Quick Start

```python
from hydrods import HydroDB

# Connect to your HydroDB server (Default: localhost:7379)
db = HydroDB(host='127.0.0.1', port=7379)

# --- Basic Operations ---

# SET a key-value pair
db.set('user:101', 'Anurag Panwar')

# GET the value
name = db.get('user:101')
print(f"Name: {name}")

# DELETE a key
db.delete('user:101')

# --- Range Queries (Ordered Keys) ---

# Fetch all keys between 'a' and 'z'
all_data = db.range('a', 'z')
print(all_data)

# Always close the connection when done
db.close()
```

## ⚡ Features
- **Raw TCP Sockets:** No overhead, blazing fast native TCP connections without blocking bugs.
- **RESP Protocol Parser:** Automatically parses Redis Serialization Protocol responses directly into Python data structures (Strings, Integers, Dictionaries).
- **Thread Pool Compatible:** Easily handles high concurrency when paired with Python's `threading` or connection pools.

## 🏆 Performance
In direct concurrent benchmarks against standard Redis Event-Loop:
- **Faster Single-Threaded Reads:** `~26,000 GET RPS` natively without async-loop overhead.
- **Extreme Concurrency:** Maintains highly stable `40,000+ RPS` even with 200+ concurrent worker threads constantly reading and writing, thanks to robust `std::shared_mutex` usage and lock-free thread queues.

## License
MIT License
