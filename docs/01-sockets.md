# 01 — Sockets & the network simulator

**Concept.** Move bytes between two programs over UDP without ever blocking the game
loop, and be able to fake a bad network on demand so every later milestone can be
tested under stress. This is the foundation everything else stands on: connections,
reliability, channels, snapshots — all of it is ultimately "put bytes in a datagram,
get bytes out of a datagram."

## Where this sits in the stack

When people say "implementing a networking stack," they usually mean the layered
machinery that turns your data into signals on a wire and back:

```
  application  ── your game: entities, inputs, snapshots
  transport    ── UDP / TCP: ports, (TCP adds) reliability & ordering   ← we live here
  network      ── IP: addressing + routing between machines
  link         ── Ethernet / Wi-Fi: framing on the local segment
  physical     ── the actual electrical / radio signals
```

The operating system owns everything from IP down: it drives the network hardware
(NIC), fills in link-layer frames, and hands packets to the routing system. **The
lowest layer we can practically program from a normal user-space app is the transport
layer** — we hand the OS a datagram plus a destination, and the kernel does the rest.
(You *can* go lower with raw sockets or eBPF/XDP, but that's kernel territory and
needs privileges; it isn't what a game does.) So "our stack" is not the whole thing —
it's the application-and-above-UDP part: the reliability, connection, channel, and
sync layers we build in the milestones that follow. We are standing on the OS's IP
implementation, not replacing it.

How a packet actually finds the far machine: the destination **IP address** is what
the network layer routes on. Your packet hops from router to router, each one looking
at the IP and forwarding it toward that network, until it reaches the destination
host; the **port** then selects which program on that host receives it. We never
implement any of that — we just supply (IP, port) and the OS and the internet
between the two machines deliver the datagram (or lose it).

## The problem

Games use UDP, not TCP. TCP guarantees ordered, reliable delivery — but to do that it
*waits*: a lost packet stalls everything queued behind it until a retransmission
arrives (head-of-line blocking). In a 60 Hz game a 200 ms-late position update is
worthless; we'd rather drop it and use the next one, which is already on its way. UDP
is "send and forget": no connection, no ordering, no retransmission, no congestion
control. That's exactly the blank slate we want — later milestones rebuild *only* the
guarantees we actually need, per data type (see 02 and 03), instead of paying for
TCP's one-size-fits-all policy.

Two hard requirements fall out of this:

- **The socket must never block.** If a receive call waits for a packet that never
  comes, the whole game freezes. So reads must be non-blocking (or equivalent): return
  "nothing waiting" immediately and let the loop get on with rendering and simulating.
- **We must be able to simulate a bad network.** Over localhost, packets are never
  lost, never reordered, never delayed — a perfect network that no real player has.
  Testing reliability, interpolation, and lag compensation *requires* injecting
  latency, jitter, loss, and duplication on purpose.

## The idea

- **Address** — an IP + port. It has to be usable as a map key so we can track peers
  ("who have I heard from, and what's their connection state?").
- **Socket** — open/bind to a port, send a datagram to an (IP, port), and *poll* for
  an incoming one, returning "nothing waiting" instead of blocking.
- **Clock** — a monotonic timer to schedule fixed ticks and measure elapsed time.
  Monotonic matters: wall-clock time can jump backward (NTP, DST) and would corrupt
  RTT and timeout math.
- **Network simulator** — wraps the socket's *send* path and holds each outgoing
  packet in a queue stamped with a delivery time, releasing it late (latency + random
  jitter), sometimes dropping it (loss), sometimes sending it twice (duplication). It
  sits between our code and the real socket, so from every later milestone's point of
  view it *is* the network — a controllable bad one.

## Why still use real sockets if we're just simulating?

Because the simulator only distorts *timing and delivery* — it does not replace the
transport. The bytes still go through a real UDP socket, to a real (IP, port), through
the OS. That's the point: we develop against a deliberately awful network but the code
path is the genuine article, so nothing has to change when we point it at a real
remote peer instead of localhost. The simulator is a lens over the send path, not a
mock of the socket. (Two separate programs talking over localhost are still two real
processes exchanging real datagrams; "simulation" here means impaired conditions, not
fake networking.)

## What we test (and why)

- **Loopback** — a datagram sent on one socket arrives byte-for-byte on another.
  *Everything is built on this; if it fails, nothing above it can work.*
- **Latency holds packets** — with delay configured, nothing arrives before its
  scheduled delivery time. *The simulator really delays instead of passing through.*
- **Statistical loss** — over many packets the observed drop rate matches the
  configured rate. *Loss injection is real and tunable, so reliability can be tested.*

## Mental model

The socket is a mail slot: you drop a numbered envelope in with an address on it and
it *might* arrive, in *some* order, or not at all — no receipt, no guarantee. The
network simulator is a mischievous postal service you install in front of your own
slot: it delays some letters, loses a few, and occasionally delivers one twice — so
you can prove your system copes before a real player's flaky Wi-Fi does it for you.

## Code & deep dive

- **C++:** `NetcodeCpp/src/net/{Address,Socket,NetworkSimulator}`, `src/core/Timer.hpp`
  · deep guide `NetcodeCpp/docs/01-sockets.md`
  · non-blocking is `fcntl(O_NONBLOCK)`; the header carries a hand-rolled `Address`.
- **Go:** `NetcodeGo/socket.go`, `NetcodeGo/simulator.go`
  · `net/netip.AddrPort` replaces the `Address` type (already comparable/hashable, so
  it's a valid map key), `time` is already monotonic (no `Timer`), and non-blocking
  receive uses a background goroutine feeding a buffered channel that the loop polls
  with a `select { … default: }` — Go has no `O_NONBLOCK`, and a past read deadline is
  a trap (it reports a timeout even when data is buffered).
