# HydroDB Python Client (`hydrods`)

The official Python client library for **HydroDB** - a blazing fast, multi-threaded, sharded NoSQL database built in C++. 

HydroDB is designed to outperform single-threaded databases like Redis in highly concurrent environments by utilizing 64-way Sharded Locks and Zero-Latency Asynchronous AOF Persistence.

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

# --- Range Queries (SkipList / Ordered Keys) ---

# Fetch all keys between 'a' and 'z'
all_data = db.range('a', 'z')
print(all_data)

# Always close the connection when done
db.close()
```

## ⚡ Features
- **Raw TCP Sockets:** No overhead, blazing fast native TCP connections.
- **RESP Protocol Parser:** Automatically parses Redis Serialization Protocol responses directly into Python data structures (Strings, Integers, Dictionaries).
- **Thread-safe Design:** Easily handles high concurrency when paired with Python's `threading` or connection pools.

## 🏆 Performance
In direct concurrent benchmarks (100,000 Operations, 100 Clients), HydroDB combined with `hydrods` achieves:
- **1.8x Faster Writes** than Redis.
- **3x Faster Parallel Reads** leveraging C++ `std::shared_mutex`.

## License
MIT License
