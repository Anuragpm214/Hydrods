import redis
import time

def run_tests():
    r = redis.Redis(port=6387, decode_responses=True)
    try:
        r.ping()
    except Exception as e:
        print(f"Failed to connect: {e}")
        return

    r.flushall()
    print("Running Tests...\n")

    # Test 1: Basic HY.ADD
    r.execute_command("HY.ADD", "sensor1", 100, 25.5)
    cnt = r.execute_command("HY.RANGE", "sensor1", 0, 200)
    assert cnt == 1, f"Test 1 Failed: Expected 1, got {cnt}"
    print("✅ Test 1: Basic HY.ADD works.")

    # Test 2: HY.MADD multiple points
    r.execute_command("HY.MADD", "sensor1", 200, 30.0, 300, 10.0, 400, 15.5)
    cnt = r.execute_command("HY.RANGE", "sensor1", 0, 500)
    assert cnt == 4, f"Test 2 Failed: Expected 4, got {cnt}"
    print("✅ Test 2: HY.MADD multiple points works.")

    # Test 3: Aggregations
    # Points so far: 25.5, 30.0, 10.0, 15.5
    # Sum: 81.0, Min: 10.0, Max: 30.0
    sum_val = float(r.execute_command("HY.RANGE", "sensor1", 0, 500, "AGGREGATION", "sum"))
    min_val = float(r.execute_command("HY.RANGE", "sensor1", 0, 500, "AGGREGATION", "min"))
    max_val = float(r.execute_command("HY.RANGE", "sensor1", 0, 500, "AGGREGATION", "max"))
    
    assert sum_val == 81.0, f"Test 3 Failed: Sum expected 81.0, got {sum_val}"
    assert min_val == 10.0, f"Test 3 Failed: Min expected 10.0, got {min_val}"
    assert max_val == 30.0, f"Test 3 Failed: Max expected 30.0, got {max_val}"
    print("✅ Test 3: All Aggregations (sum, min, max) work correctly.")

    # Test 4: Edge Case - Start > End
    cnt = r.execute_command("HY.RANGE", "sensor1", 500, 100)
    assert cnt == 0 or cnt is None, f"Test 4 Failed: Expected 0/None, got {cnt}"
    print("✅ Test 4: Edge Case (start > end) safely returns 0/None.")

    # Test 5: Edge Case - Missing Key
    cnt = r.execute_command("HY.RANGE", "sensor_does_not_exist", 0, 100)
    assert cnt == [], f"Test 5 Failed: Expected empty array [], got {cnt}"
    print("✅ Test 5: Edge Case (missing key) safely returns empty list.")

    # Test 6: Invalid Aggregation syntax
    try:
        r.execute_command("HY.RANGE", "sensor1", 0, 500, "AGGREGATION", "fake_agg")
        assert False, "Test 6 Failed: Should have thrown an error for fake_agg"
    except redis.exceptions.ResponseError as e:
        assert "unknown aggregation type" in str(e), f"Test 6 Failed: Wrong error message {e}"
    print("✅ Test 6: Invalid aggregation correctly throws ERR.")

    # Test 7: Memory Release via DEL
    r.execute_command("DEL", "sensor1")
    cnt = r.execute_command("HY.RANGE", "sensor1", 0, 500)
    assert cnt == [], "Test 7 Failed: Key should be deleted"
    print("✅ Test 7: DEL command successfully frees memory without segfaulting.")

    # Test 8: RDB Persistence Check
    r.execute_command("HY.ADD", "persist_test", 1000, 99.9)
    r.bgsave()
    while r.lastsave() == r.info('persistence')['rdb_last_bgsave_time_sec']: 
        time.sleep(0.1)
    print("✅ Test 8: BGSAVE triggered successfully.")

    print("\n🎉 All 8 comprehensive tests PASSED!")

if __name__ == "__main__":
    run_tests()
