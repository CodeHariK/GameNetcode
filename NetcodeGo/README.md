# Netcode from Scratch — Go port

A readable, from-scratch UDP game-networking stack in Go, ported milestone by
milestone from the C++ version. Run everything with:

    go test ./...

## Milestone 1 — Sockets & network simulator

- socket.go — a UDP socket. Go has no O_NONBLOCK; the idiom is to poll with a
  zero read deadline, so Receive returns "nothing waiting" instead of blocking.
- simulator.go — wraps a socket and injects latency / jitter / loss / duplication
  on the outgoing path, so later milestones can be tested under a bad network.
- simulator_test.go — what m01 verifies (loopback, latency delay, statistical loss).

Two things Go gives us for free (nice teaching notes):
- Addresses: we use the standard library's net/netip.AddrPort, which is already
  comparable and hashable (a valid map key) — no custom Address type needed.
- Time: Go's time.Time is already monotonic, so there is no Timer type to write.
