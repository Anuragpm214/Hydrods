import time
import redis
import random
import os

NUM_SYMBOLS = 10
TICKS_PER_SYMBOL = 100000  # 1M total points
BATCH_SIZE = 10000

SMALL_RANGE = 1440
MEDIUM_RANGE = 43200
LARGE_RANGE = 100000

def run_bench():
    print(f"Connecting to Redis (Port 6380) with BOTH modules loaded...")
    r = redis.Redis(host='127.0.0.1', port=6383, db=0, decode_responses=True, socket_timeout=None)
    
    # Check modules
    modules = [m['name'] for m in r.module_list()]
    print(f"Loaded Modules: {modules}")
    
    r.flushdb()
    for symbol_id in range(NUM_SYMBOLS):
        r.execute_command('TS.CREATE', f"TICKER_{symbol_id:03d}")

    print(f"\n🚀 Starting PRODUCTION Network Benchmark (1M points) 🚀")
    start_ts = int(time.time()) - TICKS_PER_SYMBOL * 60
    
    # ---------------------------------------------------------
    # 1. INGESTION
    # ---------------------------------------------------------
    redis_w_time = 0
    hydro_w_time = 0
    
    pipe = r.pipeline(transaction=False)
    
    print("Ingesting data via TCP pipeline...", flush=True)
    for symbol_id in range(NUM_SYMBOLS):
        symbol = f"TICKER_{symbol_id:03d}"
        for i in range(TICKS_PER_SYMBOL):
            ts = start_ts + i * 60
            val = 100.0 + random.uniform(-1, 1)
            
            # Redis TS (We append _TS so keys don't collide)
            pipe.execute_command('TS.ADD', f"{symbol}_TS", ts, val)
            
            # HydroDB
            pipe.execute_command('HY.ADD', f"{symbol}_HY", ts, val)
            
            if (i + 1) % BATCH_SIZE == 0:
                t1 = time.time()
                pipe.execute()
                # Divide time roughly by 2 since pipeline contains both commands
                # But to be precise, let's do two separate pipelines!
                pass

    # Wait, pipelining them together mixes times. Let's do it separately.
    r.flushdb()
    for symbol_id in range(NUM_SYMBOLS):
        r.execute_command('TS.CREATE', f"{symbol_id:03d}_TS")
    
    pipe_r = r.pipeline(transaction=False)
    for symbol_id in range(NUM_SYMBOLS):
        symbol = f"{symbol_id:03d}_TS"
        for i in range(TICKS_PER_SYMBOL):
            ts = start_ts + i * 60
            pipe_r.execute_command('TS.ADD', symbol, ts, 100.0)
            if (i+1) % BATCH_SIZE == 0:
                t1 = time.time()
                pipe_r.execute()
                redis_w_time += (time.time() - t1)

    pipe_h = r.pipeline(transaction=False)
    for symbol_id in range(NUM_SYMBOLS):
        symbol = f"{symbol_id:03d}_HY"
        for i in range(TICKS_PER_SYMBOL):
            ts = start_ts + i * 60
            pipe_h.execute_command('HY.ADD', symbol, ts, 100.0)
            if (i+1) % BATCH_SIZE == 0:
                t1 = time.time()
                pipe_h.execute()
                hydro_w_time += (time.time() - t1)

    print(f"Ingestion done! RedisTS: {redis_w_time:.2f}s | HydroDB: {hydro_w_time:.2f}s", flush=True)

    # ---------------------------------------------------------
    # 2. RANGE QUERIES
    # ---------------------------------------------------------
    def test_range(range_size, name, test_count=50):
        print(f"Testing {name}...", flush=True)
        r_time = 0
        h_time = 0
        
        for symbol_id in range(NUM_SYMBOLS):
            for t in range(test_count):
                max_start_idx = max(0, TICKS_PER_SYMBOL - range_size - 1)
                start_idx = random.randint(0, max_start_idx)
                
                s_ts = start_ts + start_idx * 60
                e_ts = s_ts + range_size * 60
                
                # Redis TS
                t1 = time.time()
                r.execute_command('TS.RANGE', f"{symbol_id:03d}_TS", s_ts, e_ts, 'AGGREGATION', 'count', 31536000000)
                r_time += (time.time() - t1)
                
                # HydroDB
                t1 = time.time()
                r.execute_command('HY.RANGE', f"{symbol_id:03d}_HY", s_ts, e_ts)
                h_time += (time.time() - t1)
                
        return r_time, h_time

    r_small, h_small = test_range(SMALL_RANGE, "SMALL RANGE", test_count=50)
    r_med, h_med = test_range(MEDIUM_RANGE, "MEDIUM RANGE", test_count=50)
    r_large, h_large = test_range(LARGE_RANGE, "LARGE RANGE", test_count=15)
    
    print("\n========= 🚀 REDIS MODULE NETWORK BENCHMARK (1M Pts) 🚀 =========")
    print(f"1. INGESTION (1M pts): RedisTS = {redis_w_time:.3f}s | HydroDB = {hydro_w_time:.3f}s")
    print(f"2. SMALL RANGE ({SMALL_RANGE} pts x 500): RedisTS = {r_small:.3f}s | HydroDB = {h_small:.3f}s")
    print(f"3. MEDIUM RANGE ({MEDIUM_RANGE} pts x 500): RedisTS = {r_med:.3f}s | HydroDB = {h_med:.3f}s")
    print(f"4. LARGE RANGE ({LARGE_RANGE} pts x 150): RedisTS = {r_large:.3f}s | HydroDB = {h_large:.3f}s")
    print("=======================================================================")

if __name__ == "__main__":
    run_bench()
