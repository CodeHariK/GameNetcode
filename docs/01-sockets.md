# 01 — Sockets & the network simulator

**Concept.** Move bytes between two programs over UDP without ever blocking the
game loop, and be able to fake a bad network for testing.

## The problem
Games use UDP, not TCP. TCP guarantees ordered, reliable delivery — but to do that
it waits and retransmits, and a late packet stalls everything behind it. In a 60 Hz
game a 200 ms-late packet is useless; better to drop it and use the next one. UDP is
"send and forget": no connection, no ordering, no retransmission. We rebuild only
the guarantees we want in later milestones. The socket must also be non-blocking so
the loop never waits on the network.

## The idea
- **Address** — an IP + port; needs to be a usable map key so we can track peers.
- **Socket** — open/bind, send a datagram, and *poll* for one (return "nothing
  waiting" instead of blocking).
- **Clock** — a monotonic timer to schedule fixed ticks and measure elapsed time.
- **Network simulator** — wraps the socket's send path and injects latency, jitter,
  packet loss, and duplication, so later milestones can be tested under stress
  (localhost is otherwise perfect).

## What we test (and why)
- **Loopback**: a datagram sent on one socket arrives byte-for-byte on another —
  everything is built on this.
- **Latency holds packets**: with delay configured, nothing arrives before its
  delivery time — the simulator really delays.
- **Statistical loss**: over many packets the observed drop rate matches the config
  — loss injection is real.

## Code & deep dive
- C++: `NetcodeCpp/src/net/{Address,Socket,NetworkSimulator}`, `src/core/Timer.hpp`
  · deep guide `NetcodeCpp/docs/01-sockets.md`
- Go: `NetcodeGo/socket.go`, `NetcodeGo/simulator.go`
  · notes: Go's `net/netip.AddrPort` replaces the Address type, `time` is already
  monotonic (no Timer), and non-blocking receive uses a goroutine + channel poll.
