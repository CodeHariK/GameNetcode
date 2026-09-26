# Game Netcode from Scratch (C++20)

A small UDP game-networking stack, built one concept at a time. Every git tag is
one milestone you can check out, build, and run:

    git tag                 # list milestones
    git checkout m04-snapshots
    make test               # (or: make run, from m04 on)

Reading order: m01 sockets -> m02 connection -> m03 channels -> m04 snapshots ->
m05 prediction -> m06 compression -> m07 lag comp -> m08 interest mgmt ->
m09 network sim.

## Milestone 1 - Sockets & network simulator
Non-blocking UDP sockets, an IP/port address type, a monotonic timer, and a
network simulator that injects latency / jitter / loss / duplication for tests.

- Files: `src/net/{Address,Socket,NetworkSimulator}`, `src/core/Timer.hpp`
- Test:  `make test`
