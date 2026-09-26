# Changelog — features added per milestone

Each milestone is a git tag you can check out, build, and run. Every tag also
carries its own deep guide in docs/ (e.g. docs/01-sockets.md). Start with
docs/00-overview.md.

## m01-sockets — Sockets & network simulator
- Added: net/Address, net/Socket, net/NetworkSimulator, core/Timer, tests/sim_test.
- Feature: non-blocking UDP send/receive, an IP/port value type, a monotonic clock,
  and a simulator that injects latency / jitter / loss / duplication for testing.

## m02-connection — Virtual connection & reliability
- Added: net/PacketHeader, net/ReliabilitySystem, net/Connection, tests/protocol_test.
- Feature: a 12-byte packet header (sequence + ack + ack-bits), RTT and loss
  estimation, and a connection state machine with heartbeats and timeouts.

## m03-channels — Channel multiplexing
- Added: net/Channel, net/ChannelTypes, the three channel implementations,
  tests/channel_test.
- Feature: UnreliableUnordered / UnreliableSequenced / ReliableOrdered channels
  packed into one datagram; Connection multiplexes them.

## m04-snapshots — Authoritative server & snapshot interpolation
- Added: game/GameTypes, game/SnapshotBuffer, apps/server, apps/client.
- Feature: 60 Hz authoritative sim, 20 Hz raw snapshots, and client-side
  interpolation ~100 ms in the past. First runnable make run.

## m05-prediction — Client prediction & reconciliation
- Added: game/Simulation, game/InputHistory, tests/prediction_test; input types
  and last_client_input_tick in GameTypes.
- Feature: zero-latency local prediction, redundant input batches, and server
  reconciliation (snap + replay) on divergence.

## m06-compression — Bit packing & delta compression
- Added: net/BitStream, game/DeltaSnapshot, tests/bitstream_test, tests/delta_test;
  ack_server_tick in InputBatchHeader.
- Feature: arbitrary-bit-width packing + quantization, and baseline-relative delta
  snapshots (~85% smaller).

## m07-lagcomp — Lag compensation (hit rewind)
- Added: game/LagCompensation, tests/lagcomp_test; FireCommand, HitNotification,
  ReliableMsgType in GameTypes.
- Feature: per-tick world history; shots rewind to what the shooter saw before
  hit-testing. Server bot to shoot at.

## m08-interest — Interest management (area of interest)
- Added: game/InterestManagement, tests/aoi_test.
- Feature: per-client AOI culling (radius + nearest-N cap) with per-client delta
  baselines; full-world history kept for fair hit tests. Multiple bots.

## m09-netsim — Network simulator in the live loop
- Added: game/SimConfig, tests/netsim_test; Connection simulator hook.
- Feature: route outgoing packets through the simulator (env-configured), so every
  earlier technique can be seen working under latency/jitter/loss. make run-sim.
