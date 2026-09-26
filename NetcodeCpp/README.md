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

## Milestone 2 - Virtual connection & reliability
12-byte packet header (protocol id, sequence, ack, ack-bits), Glenn Fiedler's
sliding-window acks, RTT + packet-loss estimation, and a Connection state machine
with heartbeats and timeouts.

- Files: `src/net/{PacketHeader,ReliabilitySystem,Connection}`
- Test:  `make test` (adds protocol_test)

## Milestone 3 - Channels (delivery guarantees)
Three delivery channels multiplexed into one datagram: UnreliableUnordered
(fire-and-forget), UnreliableSequenced (drop old), ReliableOrdered (retransmit +
in-order). Connection now packs/unpacks channel messages.

- Files: `src/net/{Channel,ChannelTypes,UnreliableUnordered,UnreliableSequenced,ReliableOrdered}Channel`
- Test:  `make test` (adds channel_test)

## Milestone 4 - Authoritative server & snapshots
The server simulates the world at 60Hz and broadcasts raw snapshots at 20Hz. The
client buffers snapshots and interpolates entities ~100ms in the past for smooth
motion despite the low send rate. First runnable demo: `make run`.

- Files: `src/game/{GameTypes,SnapshotBuffer}`, `src/apps/{server,client}`
- Run:   `make run`

## Milestone 5 - Client prediction & reconciliation
The client applies input immediately (zero latency) and records it. The server is
authoritative and stamps each snapshot with the last input it processed. On a
mismatch the client snaps to the server state and replays pending inputs.

- Files: `src/game/{Simulation,InputHistory}`
- Test:  `make test` (adds prediction_test)  |  Run: `make run`

## Milestone 6 - Bit packing & delta compression
Snapshots are bit-packed and delta-encoded against a baseline the client has
acknowledged: one changed-bit per field, quantized values, colours collapse to a
bit. Big bandwidth win. `BitWriter`/`BitReader` do the packing.

- Files: `src/net/BitStream`, `src/game/DeltaSnapshot`
- Test:  `make test` (adds bitstream_test, delta_test)
