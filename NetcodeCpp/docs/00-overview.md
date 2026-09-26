# Game Netcode from Scratch — Learning Guide

A small but real UDP game-networking stack in C++20, built one concept at a time.
Every milestone is a git tag you can check out, build, and run — and each tag
carries its own deep guide in this docs/ folder.

    git tag                                   # list milestones (m01 .. m09)
    git checkout m05-prediction               # jump to that milestone's code + docs
    make test                                 # run the tests that exist at this point
    make run                                  # run server + client (from m04 on)
    git diff m05-prediction m06-compression   # see exactly what a milestone added

## How the code is laid out

    src/net/     the reusable networking library (sockets, connection, reliability,
                 channels, bitstream, network simulator)
    src/game/    game-specific logic (types, simulation, prediction, delta, lag
                 compensation, interest management) — header-only
    src/apps/    the demo programs: server.cpp and client.cpp
    tests/       one test program per concept
    docs/        these guides

Mental model: src/net/ is the engine, src/game/ is this game, and src/apps/ wires
them together.

## Reading order (each has a deep guide)

1. [01 — Sockets & the network simulator](01-sockets.md)
2. [02 — Virtual connection & reliability](02-connection.md)
3. [03 — Channels (delivery guarantees)](03-channels.md)
4. [04 — Authoritative server & snapshots](04-snapshots.md)
5. [05 — Client prediction & reconciliation](05-prediction.md)
6. [06 — Bit packing & delta compression](06-compression.md)
7. [07 — Lag compensation (hit rewind)](07-lag-compensation.md)
8. [08 — Interest management (area of interest)](08-interest-management.md)
9. [09 — Network simulator in the live loop](09-network-sim.md)

Also: [CHANGELOG.md](CHANGELOG.md) (what each milestone added) and
[CODE_REVIEW.md](CODE_REVIEW.md) (strengths, rough edges, where to simplify).

## Build & run

    make            # build apps + tests
    make test       # build and run every test present at this milestone
    make run        # server + client on a clean local network (m04+)
    make run-sim    # server + client under a simulated bad network (m09)
    make clean      # delete build/ and bin/

Everything is plain C++20 + a Makefile — no external dependencies.
