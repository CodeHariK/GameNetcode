CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O3 -Isrc

# Network library sources (grows as milestones add capabilities).
LIB_SRCS = src/net/Address.cpp src/net/Socket.cpp src/net/NetworkSimulator.cpp \
           src/net/ReliabilitySystem.cpp src/net/Connection.cpp

TESTS = bin/sim_test bin/protocol_test

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

bin/%: tests/%.cpp $(LIB_SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB_SRCS) -o $@

clean:
	rm -rf bin build
