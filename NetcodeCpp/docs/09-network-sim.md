# Milestone 9 — Network simulator in the live loop

**What this milestone adds:** actually *experience* latency, jitter, and loss, so
everything built in m1–m8 can be seen doing its job. The `NetworkSimulator` from m1
is wired into the live connection.

New files: `src/game/SimConfig`, `tests/netsim_test.cpp`. `Connection` gains an
optional simulator hook.

## The idea

Up to now the simulator only existed in tests; on localhost the live demo runs on a
perfect network, so prediction, delta compression and lag comp look unnecessary. Now
a `Connection` can route its **outgoing** packets through the simulator instead of
straight to the socket:
- `Connection::set_network_simulator(sim)` — when set, `send_packet`/`flush_channels`
  hand packets to the simulator; the app calls `sim.update()` each frame to flush the
  ones whose delivery time has come.
- With **no** simulator attached, nothing changes — it's a transparent passthrough,
  so all earlier tests are unaffected.

Both sides can be impaired independently: the server's connections impair
server→client, the client's connection impairs client→server.

## The pieces

### `Connection` hook
A forward-declared `NetworkSimulator*` member and `set_network_simulator`; the two
send sites choose `sim_->send_packet(...)` when set, else `socket_.send(...)`.

### `SimConfig`
Reads impairment from environment variables so the apps' command line doesn't
change: `NETSIM_LATENCY_MS`, `NETSIM_JITTER_MS`, `NETSIM_LOSS`, `NETSIM_DUP`.
`read_sim_config_from_env()` + `sim_enabled()`.

### Apps
Each side, when those env vars are set, creates a simulator, attaches it to its
connection, and flushes it each frame. A startup line reports the active impairment.

## What `netsim_test` verifies, and why

An integration test over loopback: a `Connection` with an attached simulator
- **delays delivery** — nothing arrives before the latency window elapses, then does
  after `update()` past it; and
- **drops at 100% loss** — nothing ever arrives.

*This proves the Connection↔simulator wiring itself (the raw simulator was already
covered by `sim_test` in m01).*

## Run it

```
make test        # adds netsim_test
make run-sim     # both sides under 80ms +/-20ms, 5% loss, 2% dup
make run-sim NETSIM_LOSS=0.5 NETSIM_JITTER_MS=30   # crank it to force reconciliations
```

What you'll see: under a mild link, ping climbs (~180 ms round trip) but redundant
input batches absorb the loss so reconciliations stay near 0. Under 50% loss,
reconciliations start firing and recovering, yet hit rate stays high thanks to lag
compensation and the reliable channel — every earlier milestone paying off at once.

## Mental model

You've built a car with airbags, ABS, and crumple zones on a flawless test track.
This milestone is the skid pad: turn on rain and potholes and watch each safety
system finally do something.
