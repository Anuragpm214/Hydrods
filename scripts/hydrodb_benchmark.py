import socket
import time

def run_benchmark():
    print("==============================================")
    print("      🌊 HydroDB Network Benchmark Tool      ")
    print("==============================================")
    print("Connecting to localhost:7379...")
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(('127.0.0.1', 7379))
    except ConnectionRefusedError:
        print("❌ Could not connect! Make sure the HydroDB server is running.")
        return

    # Read the welcome message
    welcome = s.recv(1024)
    print(welcome.decode().strip())

    NUM_OPS = 20000 # Number of commands to test per operation
    
    print(f"\n🚀 1. Running {NUM_OPS} SET operations...")
    start_time = time.time()
    for i in range(NUM_OPS):
        cmd = f"SET user{i} data{i}\n".encode()
        s.sendall(cmd)
        s.recv(1024) # Wait for "+OK"
    set_time = time.time() - start_time
    print(f"✅ SET done in {set_time:.2f}s! Speed: {NUM_OPS/set_time:.0f} Operations/Sec")

    print(f"\n🚀 2. Running {NUM_OPS} GET operations...")
    start_time = time.time()
    for i in range(NUM_OPS):
        cmd = f"GET user{i}\n".encode()
        s.sendall(cmd)
        s.recv(1024) # Wait for response
    get_time = time.time() - start_time
    print(f"✅ GET done in {get_time:.2f}s! Speed: {NUM_OPS/get_time:.0f} Operations/Sec")

    print("\n🚀 3. Running 1000 ZRANGE operations...")
    start_time = time.time()
    for i in range(1000):
        # Searching between user1000 and user2000
        cmd = b"ZRANGE user1000 user2000\n"
        s.sendall(cmd)
        s.recv(65536) # Read large range chunk
    range_time = time.time() - start_time
    print(f"✅ ZRANGE done in {range_time:.2f}s! Speed: {1000/range_time:.0f} Operations/Sec")

    print(f"\n🚀 4. Running {NUM_OPS} DEL operations...")
    start_time = time.time()
    for i in range(NUM_OPS):
        cmd = f"DEL user{i}\n".encode()
        s.sendall(cmd)
        s.recv(1024)
    del_time = time.time() - start_time
    print(f"✅ DEL done in {del_time:.2f}s! Speed: {NUM_OPS/del_time:.0f} Operations/Sec")

    s.sendall(b"QUIT\n")
    s.close()
    print("\n==============================================")
    print("             Benchmark Complete!              ")
    print("==============================================")

if __name__ == "__main__":
    run_benchmark()
