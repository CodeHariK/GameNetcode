# Milestone 1 — Sockets & the network simulator

**What this milestone adds:** the raw plumbing to move bytes between two programs
over UDP without ever blocking the game loop, plus a tool to *fake* a bad network
so every later milestone can be tested under realistic conditions.

New files: `src/net/Address`, `src/net/Socket`, `src/net/NetworkSimulator`,
`src/core/Timer`, `tests/sim_test.cpp`.

## Why UDP, and why non-blocking?

Games use **UDP**, not TCP. TCP guarantees every byte arrives in order, but to do
that it *waits* and *retransmits* — a late packet stalls everything behind it. In
a 60 Hz game a packet that's 200 ms late is worthless; we'd rather drop it and use
the next one. UDP is "send and forget": no connection, no ordering, no
retransmission. We rebuild exactly the guarantees we want (and nothing more) in
later milestones.

The socket is also **non-blocking**: reading it returns immediately whether or not
a packet is waiting. A game loop must run every ~16 ms; it can never sit and wait
on the network.

## The pieces

### `Address` — who to send to
A small value type wrapping an IPv4 host + port (stored in host byte order). UDP's
`sendto`/`recvfrom` speak in addresses, and we need to recognise peers we've seen
before, so `Address` is comparable and hashable (it's used as a `std::unordered_map`
key to track connections). Key API: the constructors (incl. from an
`"a.b.c.d", port` string), `host()`, `port()`, `to_string()`, `operator==`, and a
`std::hash` specialisation.

### `Socket` — the pipe
An RAII wrapper around a POSIX UDP socket, put into non-blocking mode with
`fcntl(O_NONBLOCK)`. Key API:
- `open(port)` — bind to a port (`0` = let the OS pick one, used by clients).
- `send(dest, data, size)` — fire a datagram at an address.
- `receive(sender, buf, cap)` — returns **>0** bytes received, **0** if nothing is
  waiting (not an error!), **<0** on a real error. Fills `sender` with who sent it.
- `bound_port()`, `close()`.

The "0 means nothing yet" contract is what lets the game loop poll the socket every
frame and move on instantly.

### `Timer` — the clock
A monotonic clock (`Timer::now_seconds()`) plus `sleep_ms()`. We use a *monotonic*
clock, not wall-clock time, because netcode constantly measures elapsed time and
schedules fixed 60 Hz ticks; wall-clock time can jump backwards (NTP, DST) and
would corrupt those measurements.

### `NetworkSimulator` — a bad network on demand
Wraps a `Socket`. Instead of sending immediately, `send_packet(dest, data, size,
now)` rolls dice against the configured **loss** and **duplicate** rates and, for
survivors, schedules delivery at `now + latency ± jitter` in a priority queue
ordered by delivery time. Each frame you call `update(now)`, which flushes any
packets whose delivery time has arrived to the real socket. Config fields:
`latency_ms`, `jitter_ms`, `packet_loss_rate`, `duplicate_rate`; it also keeps
`stats()` (attempted/dropped/transmitted/duplicated).

Why build this now? On localhost the network is perfect — prediction, delta
compression and lag compensation would all look pointless. The simulator lets us
dial in latency/jitter/loss so those techniques visibly earn their keep (it goes
into the live loop in Milestone 9).

## What `sim_test` verifies, and why it matters

1. **Basic loopback** — a string sent from one socket is received byte-for-byte by
   another. Proves `Socket` send/receive works and that "nothing waiting" returns 0.
   *Everything else is built on this.*
2. **Latency holds packets** — with 30 ms latency configured, immediately after
   sending, **zero** packets have arrived. Proves the simulator actually *delays*
   delivery instead of passing packets straight through.
3. **Statistical loss** — over 500 packets at a 20% drop rate, the observed drop
   rate lands near 20%. Proves loss injection is real and roughly matches config,
   so later tests that rely on "5% loss" mean something.

## Run it

```
make test        # builds and runs sim_test
```

## Mental model

`Address` is the envelope; `Socket` is a mailbox you *peek* into (never wait at);
`Timer` is the clock everything schedules against; `NetworkSimulator` is a
deliberately unreliable postal service you switch on for testing.
