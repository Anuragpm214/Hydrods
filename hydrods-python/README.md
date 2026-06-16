<div align="center">

# 🐍 hydrods

### The Official Python Client for HydroDB

*Zero dependencies. Pure TCP. Blazing fast.*

<br/>

[![PyPI](https://img.shields.io/pypi/v/hydrods?style=for-the-badge&color=blue)](https://pypi.org/project/hydrods/)
[![Python](https://img.shields.io/pypi/pyversions/hydrods?style=for-the-badge&logo=python&logoColor=white)](https://pypi.org/project/hydrods/)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](https://github.com/Anuragpm214/Hydrods/blob/main/LICENSE)

</div>

---

## 🤔 What is this?

`hydrods` is a lightweight Python SDK to connect to [**HydroDB**](https://github.com/Anuragpm214/Hydrods) — a high-performance NoSQL database engine built from scratch in C++17.

- **No `redis-py` needed** — talks directly over raw TCP sockets
- **RESP protocol parser** — automatically converts responses into Python types (`str`, `int`, `dict`)
- **Thread-safe** — works great with `threading`, `concurrent.futures`, or connection pools

---

## 🚀 Installation

```bash
pip install hydrods
```

---

## 💻 Quick Start

```python
from hydrods import HydroDB

# Connect to your HydroDB server
db = HydroDB(host='127.0.0.1', port=7379)

# ✏️ SET — store a value
db.set('user:101', 'Anurag Panwar')

# 📖 GET — retrieve it
name = db.get('user:101')
print(name)  # → "Anurag Panwar"

# 🗑️ DELETE — remove it
db.delete('user:101')

# 📊 RANGE — query a lexicographic range
db.set('sensor:001', 'temp: 22.5')
db.set('sensor:002', 'temp: 23.1')
db.set('sensor:003', 'temp: 21.8')

results = db.range('sensor:000', 'sensor:999')
for key, value in results.items():
    print(f"  {key} → {value}")

# 📄 RANGE with pagination
page = db.range('sensor:000', 'sensor:999', limit=10, offset=0)

# 🔌 Close when done
db.close()
```

---

## 📖 API Reference

| Method | Returns | Description |
|:-------|:--------|:------------|
| `HydroDB(host, port)` | — | Connect to HydroDB server (default: `127.0.0.1:7379`) |
| `set(key, value)` | `bool` | Store a key-value pair |
| `get(key)` | `str \| None` | Retrieve value by key, `None` if not found |
| `delete(key)` | `bool` | Delete a key, returns `True` if deleted |
| `range(start, end)` | `dict` | Get all key-value pairs in lexicographic range |
| `range(start, end, limit, offset)` | `dict` | Paginated range query |
| `close()` | — | Close the TCP connection |

---

## ⚡ Why hydrods?

| Feature | `hydrods` | `redis-py` |
|:--------|:---------:|:----------:|
| External dependencies | ❌ None | ✅ hiredis, etc. |
| Direct TCP sockets | ✅ | ❌ (abstracted) |
| RESP parsing | ✅ Built-in | ✅ Built-in |
| Range queries (ordered) | ✅ Native `HRANGE` | ⚠️ Needs ZRANGEBYLEX |
| Pagination support | ✅ `LIMIT offset count` | ⚠️ Varies by command |
| Thread-safe usage | ✅ | ✅ |

---

## 🔗 Links

- **HydroDB Server** → [github.com/Anuragpm214/Hydrods](https://github.com/Anuragpm214/Hydrods)
- **PyPI** → [pypi.org/project/hydrods](https://pypi.org/project/hydrods/)

---

## 📜 License

MIT License — [Anurag Panwar](https://github.com/Anuragpm214)
