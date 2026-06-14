CXX = g++
CXXFLAGS = -std=c++17 -O3 -Iinclude -pthread

.PHONY: all server test benchmark clean

all: server

server:
	$(CXX) $(CXXFLAGS) src/server.cpp -o hydrodb_server

test:
	$(CXX) $(CXXFLAGS) tests/test_hydrods_hpp.cpp -o test_hydrods
	./test_hydrods

benchmark:
	$(CXX) $(CXXFLAGS) tests/benchmark_compare.cpp -o run_benchmark
	./run_benchmark

clean:
	rm -f hydrodb_server test_hydrods run_benchmark
