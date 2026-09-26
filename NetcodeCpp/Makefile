CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O3 -Isrc

# Milestone 1: non-blocking UDP sockets + network simulator.
LIB_SRCS = src/net/Address.cpp src/net/Socket.cpp src/net/NetworkSimulator.cpp

.PHONY: test clean
test:
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) tests/sim_test.cpp $(LIB_SRCS) -o bin/sim_test
	./bin/sim_test

clean:
	rm -rf bin build
