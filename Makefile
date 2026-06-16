CXX = g++
CXXFLAGS = -std=c++17 -O3 -Iinclude -pthread

.PHONY: all server clean

all: server

server:
	$(CXX) $(CXXFLAGS) src/server.cpp -o hydrodb_server

clean:
	rm -f hydrodb_server
