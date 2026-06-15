import redis
import sys
import time
import subprocess
import random

def run_bench():
    print("Starting Redis with HydroDB module on port 6389...")
    redis_proc = subprocess.Popen(['redis-server', '--port', '6389', '--loadmodule', './hydrodb.so'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    
    try:
        r = redis.Redis(host='127.0.0.1', port=6389, db=0, decode_responses=True)
        r.ping()
        
        TOTAL_POINTS = 500000
        hydro_key = "hydro_bench"
        zset_key = "zset_bench"
        
        print("\n" + "="*65)
        print("  1. INSERTION SPEED & MEMORY BENCHMARK (500K Points)")
        print("="*65)
        
        r.flushall()
        
        # INSERT HYDRO
        start_t = time.time()
        pipe = r.pipeline(transaction=False)
        for i in range(TOTAL_POINTS):
            pipe.execute_command('HY.ADD', hydro_key, 1000000 + i, 1.23)
            if i % 20000 == 0: pipe.execute()
        pipe.execute()
        hydro_ins_time = time.time() - start_t
        
        # INSERT ZSET
        start_t = time.time()
        pipe = r.pipeline(transaction=False)
        for i in range(TOTAL_POINTS):
            pipe.zadd(zset_key, {f"{1000000+i}:1.23": 1000000+i})
            if i % 20000 == 0: pipe.execute()
        pipe.execute()
        zset_ins_time = time.time() - start_t
        
        hydro_mem = r.execute_command('MEMORY', 'USAGE', hydro_key)
        zset_mem = r.execute_command('MEMORY', 'USAGE', zset_key)
        
        print(f"HydroDB Insert Time : {hydro_ins_time:.3f} s")
        print(f"ZSET Insert Time    : {zset_ins_time:.3f} s")
        print(f"HydroDB Memory      : {hydro_mem/1024/1024:.2f} MB")
        print(f"ZSET Memory         : {zset_mem/1024/1024:.2f} MB")
        
        
        print("\n" + "="*65)
        print("  2. SEARCH / POINT LOOKUP (1,000 Random Queries)")
        print("="*65)
        queries = [random.randint(1000000, 1000000 + TOTAL_POINTS - 1) for _ in range(1000)]
        
        start_t = time.time()
        pipe = r.pipeline(transaction=False)
        for q in queries:
            pipe.execute_command('HY.RANGE', hydro_key, q, q, 'AGGREGATION', 'count')
        pipe.execute()
        hydro_search_time = time.time() - start_t
        
        start_t = time.time()
        pipe = r.pipeline(transaction=False)
        for q in queries:
            pipe.zcount(zset_key, q, q)
        pipe.execute()
        zset_search_time = time.time() - start_t
        
        print(f"HydroDB Search Time : {hydro_search_time*1000:.2f} ms")
        print(f"ZSET Search Time    : {zset_search_time*1000:.2f} ms")
        

        print("\n" + "="*65)
        print("  3. RANGE QUERIES (100 Random Queries Each)")
        print("="*65)
        
        def bench_range(size, name):
            # HydroDB (Server-side aggregation)
            start_t = time.time()
            pipe = r.pipeline(transaction=False)
            for _ in range(100):
                start = random.randint(1000000, 1000000 + TOTAL_POINTS - size - 1)
                pipe.execute_command('HY.RANGE', hydro_key, start, start + size, 'AGGREGATION', 'sum')
            pipe.execute()
            hydro_time = time.time() - start_t
            
            # ZSET (Client-side aggregation: Fetch + Sum)
            start_t = time.time()
            for _ in range(10): # Run only 10 times for ZSET because it's too slow with network overhead
                start = random.randint(1000000, 1000000 + TOTAL_POINTS - size - 1)
                # Fetch all elements in range
                elements = r.zrangebyscore(zset_key, start, start + size)
                # Client-side aggregation
                total_sum = sum(float(e.split(':')[1]) for e in elements)
            zset_time = (time.time() - start_t) * 10 # Scale up to 100 queries for fair comparison
            
            print(f"[{name:<18}] HydroDB: {hydro_time*1000:>8.2f} ms | ZSET (Network+Sum): {zset_time*1000:>8.2f} ms")
        
        bench_range(1000, "Small Range (1K)")
        bench_range(50000, "Medium Range (50K)")
        bench_range(200000, "Large Range (200K)")

        print("\n" + "="*65)
        print("  4. EVICTION / DELETION (Removing Oldest 100K Points)")
        print("="*65)
        
        # ZSET Eviction using ZREMRANGEBYSCORE
        start_t = time.time()
        r.zremrangebyscore(zset_key, 1000000, 1000000 + 100000 - 1)
        zset_evict_time = time.time() - start_t
        
        # HydroDB Eviction using RETENTION (Newest is ~1.5M. We want to cut at 1.1M, so retention=400,000)
        start_t = time.time()
        r.execute_command('HY.ADD', hydro_key, 1000000 + TOTAL_POINTS, 1.23, 'RETENTION', 400000)
        hydro_evict_time = time.time() - start_t
        
        print(f"HydroDB Eviction (O(1) Bucket Drop) : {hydro_evict_time*1000:.2f} ms")
        print(f"ZSET Eviction (ZREMRANGEBYSCORE)    : {zset_evict_time*1000:.2f} ms")

    except Exception as e:
        print(f"Error: {e}")
    finally:
        print("\nShutting down Redis...")
        redis_proc.terminate()
        redis_proc.wait()

if __name__ == '__main__':
    run_bench()
