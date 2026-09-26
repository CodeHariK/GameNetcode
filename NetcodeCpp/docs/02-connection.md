# Milestone 2 — Virtual connection & reliability

**What this milestone adds:** UDP has no concept of a "connection" or of knowing
whether the other side got your packet. This milestone builds both on top of raw
UDP: a per-packet header with acknowledgements, RTT/loss estimation, and a
connection state machine with heartbeats and timeouts.

New files: `src/net/PacketHeader`, `src/net/ReliabilitySystem`,
`src/net/Connection`, `tests/protocol_test.cpp`.

## The problem

With raw UDP you send a datagram and hear nothing back — you don't know if it
arrived, how long the round trip took, or whether the peer is even still there. We
need those facts to build reliability and to measure the network, so we add a tiny
header to every packet.

## The pieces

### `PacketHeader` — 12 bytes on the front of every packet
Layout: `protocol_id | sequence | ack | ack_bits`.
- `protocol_id` — a magic number; packets that don't start with it are ignored
  (cheap filter against stray/foreign traffic).
- `sequence` — our own packet counter, incremented per packet.
- `ack` — the highest `sequence` we've received *from them*.
- `ack_bits` — a 32-bit field where bit *n* means "I also received packet
  `ack - n`."

`serialize`/`deserialize` convert to/from network byte order and validate the
magic. Sequence numbers are 16-bit and **wrap around**, so comparing them ("is A
newer than B?") needs wrap-aware logic — a classic netcode bug source.

### `ReliabilitySystem` — acks, RTT, and loss from those headers
One `ack` plus 32 `ack_bits` means a single received packet acknowledges up to 33
of the peer's recent packets, so acknowledgements survive packet loss well
(Glenn Fiedler's scheme). From the acks it derives:
- **RTT** — a smoothed (exponential moving average) round-trip time.
- **packet loss %** — from packets that were sent but never acknowledged in time.

It also exposes an **ack callback** so higher layers (channels, m03) can learn
"packet N got through" and stop retransmitting the messages that rode in it.

### `Connection` — a session over connectionless UDP
Wraps a socket + a `ReliabilitySystem` and adds a state machine:
`Disconnected → Connecting → Connected → Disconnecting`. Responsibilities:
- `connect()` / `accept()` — start a session as client / server.
- `send_packet()` — wrap a payload in a fresh header and send.
- `process_packet()` — validate + feed the header to reliability, flip Connecting
  → Connected on first reply, and hand the raw payload up.
- `update()` — every frame: send a **heartbeat** if we've been quiet (so the peer
  knows we're alive), and **time out** the connection if we've heard nothing for a
  few seconds.

At this milestone `Connection` carries raw payloads only; channels arrive in m03.

## What `protocol_test` verifies, and why

1. **Sequence wraparound** — the "is newer" comparison is correct across the 16-bit
   wrap. *A wrong comparison silently corrupts acks once the counter wraps.*
2. **Header (de)serialization + magic** — a header survives a round trip and a
   corrupt/foreign packet is rejected. *The wire format must be exact and safe.*
3. **Ack bitfield + RTT** — after a simulated exchange, the right packets are marked
   acknowledged and RTT is measured (~40 ms in the test). *This is the foundation
   reliability and the network stats stand on.*

## Run it

```
make test        # adds protocol_test
```

## Mental model

The 12-byte header is a postcard that always says "I'm postcard #S, and by the way
I've received your postcards up to #A plus these earlier ones." From a stream of
those postcards each side figures out what got through, how fast, and whether the
other side is still writing.
