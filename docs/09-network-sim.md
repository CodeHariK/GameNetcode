# 09 — Network simulator in the live loop

**Concept.** Actually experience latency, jitter, and loss so every earlier
technique can be seen doing its job.

## The idea
Wire the network simulator (from 01) into the live connection: outgoing packets are
routed through it (delayed/jittered/dropped/duplicated) instead of straight to the
socket, and the app flushes it each frame. With no simulator attached it's a
transparent passthrough, so existing tests are unaffected. Both sides can be impaired
independently, configured from environment variables.

## What we test (and why)
- An integration test: a connection with an attached simulator delays delivery until
  updated past the latency window, and drops everything at 100% loss — proving the
  connection↔simulator wiring (the raw simulator was covered in 01).

## What you see
Under a mild link, RTT climbs but redundant input batches absorb loss, so
reconciliations stay near zero. Under heavy loss, reconciliations fire and recover,
yet hit rate stays high thanks to lag compensation and the reliable channel — every
earlier milestone paying off at once.

## Code & deep dive
- C++: `NetcodeCpp/src/game/SimConfig` + the `Connection` hook
  · deep guide `NetcodeCpp/docs/09-network-sim.md` · run: `cd NetcodeCpp && make run-sim`
- Go: not yet ported (the simulator itself already exists — `NetcodeGo/simulator.go`).
