CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O3 -Isrc
PORT ?= 40000

# Network library sources (grows as milestones add capabilities).
LIB_SRCS = src/net/Address.cpp src/net/Socket.cpp src/net/NetworkSimulator.cpp \
           src/net/ReliabilitySystem.cpp src/net/Connection.cpp

TESTS = bin/sim_test bin/protocol_test bin/channel_test bin/prediction_test bin/bitstream_test bin/delta_test bin/lagcomp_test bin/aoi_test
APPS  = bin/server bin/client

.PHONY: all test run clean
all: $(TESTS) $(APPS)

test: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

# Server in the background, client in the foreground; server stops on exit.
run: $(APPS)
	@./bin/server $(PORT) & SRV=$$!; \
	trap 'kill $$SRV 2>/dev/null' EXIT INT TERM; \
	sleep 1; ./bin/client $(PORT)

bin/%: tests/%.cpp $(LIB_SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB_SRCS) -o $@

bin/server: src/apps/server.cpp $(LIB_SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB_SRCS) -o $@

bin/client: src/apps/client.cpp $(LIB_SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB_SRCS) -o $@

clean:
	rm -rf bin build
