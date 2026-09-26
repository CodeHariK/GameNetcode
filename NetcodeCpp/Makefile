CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O3 -Isrc
PORT ?= 40000

# Network library sources (grows as milestones add capabilities).
LIB_SRCS = src/net/Address.cpp src/net/Socket.cpp src/net/NetworkSimulator.cpp \
           src/net/ReliabilitySystem.cpp src/net/Connection.cpp

TESTS = bin/sim_test bin/protocol_test bin/channel_test bin/prediction_test bin/bitstream_test bin/delta_test bin/lagcomp_test bin/aoi_test bin/netsim_test
APPS  = bin/server bin/client

.PHONY: all test run run-sim clean
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

# Both sides under a simulated adverse network (env-driven).
NETSIM_LATENCY_MS ?= 80
NETSIM_JITTER_MS  ?= 20
NETSIM_LOSS       ?= 0.05
NETSIM_DUP        ?= 0.02
run-sim: $(APPS)
	@echo "Simulated net: $(NETSIM_LATENCY_MS)ms +/-$(NETSIM_JITTER_MS)ms, loss $(NETSIM_LOSS), dup $(NETSIM_DUP)"; \
	NETSIM_LATENCY_MS=$(NETSIM_LATENCY_MS) NETSIM_JITTER_MS=$(NETSIM_JITTER_MS) NETSIM_LOSS=$(NETSIM_LOSS) NETSIM_DUP=$(NETSIM_DUP) ./bin/server $(PORT) & SRV=$$!; \
	trap 'kill $$SRV 2>/dev/null' EXIT INT TERM; sleep 1; \
	NETSIM_LATENCY_MS=$(NETSIM_LATENCY_MS) NETSIM_JITTER_MS=$(NETSIM_JITTER_MS) NETSIM_LOSS=$(NETSIM_LOSS) NETSIM_DUP=$(NETSIM_DUP) ./bin/client $(PORT)

clean:
	rm -rf bin build
