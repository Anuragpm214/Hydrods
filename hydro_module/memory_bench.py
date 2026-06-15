import redis
import sys
import time
import subprocess

def run_bench():
    print("Starting Redis with HydroDB module on port 6389...")
    redis_proc = subprocess.Popen(['redis-server', '--port', '6389', '--loadmodule', './hydrodb.so'])
    time.sleep(1) # wait for redis to start
    
    try:
        r = redis.Redis(host='127.0.0.1', port=6389, db=0, decode_responses=True)
        r.ping()
        
        ranges = [1000, 10000, 100000, 500000]
        
        print(f"\n{'Points Count':<15} | {'HydroDB Memory':<15} | {'Redis ZSET Memory':<18} | {'Comparison'}")
        print("-" * 75)
        
        for count in ranges:
            r.flushdb()
            hydro_key = "hydro_data"
            zset_key = "zset_data"
            
            # Insert into HydroDB
            pipe = r.pipeline(transaction=False)
            for i in range(count):
                pipe.execute_command('HY.ADD', hydro_key, 1000000 + i, 1.234)
            pipe.execute()
            
            # Insert into Redis ZSET
            pipe = r.pipeline(transaction=False)
            for i in range(count):
                pipe.zadd(zset_key, {f"{1000000+i}:1.234": 1000000+i})
            pipe.execute()
            
            # Get memory usage
            hydro_mem = r.execute_command('MEMORY', 'USAGE', hydro_key)
            zset_mem = r.execute_command('MEMORY', 'USAGE', zset_key)
            
            ratio = zset_mem / hydro_mem if hydro_mem else 0
            diff = "HydroDB saves {:.0f}% memory".format((1 - 1/ratio) * 100) if ratio > 1 else "HydroDB uses more"
            
            print(f"{count:<15} | {hydro_mem:<15} | {zset_mem:<18} | {diff}")
            
    finally:
        print("\nShutting down Redis...")
        redis_proc.terminate()
        redis_proc.wait()

if __name__ == '__main__':
    run_bench()
