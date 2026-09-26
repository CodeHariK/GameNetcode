# Milestone 2 — Virtual connection & reliability

**What this milestone adds:** UDP has no concept of a "connection," and no way to
know whether the other side received your packet, how long the round trip took, or
whether the peer is still alive. This milestone builds all of that on top of raw UDP:
a 12-byte per-packet header carrying acknowledgements, a reliability system that
turns those headers into RTT/loss facts, and a connection state machine with
heartbeats and timeouts.

We are, deliberately, re-implementing the *useful bookkeeping* of TCP (sequence
numbers + acks) while leaving out the part that stalls the game — TCP holds
everything behind a lost packet until it's retransmitted; we never do that.

New files: `src/net/PacketHeader.hpp`, `src/net/ReliabilitySystem.{hpp,cpp}`,
`src/net/Connection.{hpp,cpp}`, `tests/protocol_test.cpp`.

## The problem

After milestone 1 we can send and poll for UDP datagrams. But `socket.send(...)`
tells us nothing afterward: no ACK, no timing, no feedback on which of our own
packets arrived, no signal that the peer vanished. Reliable messages, ping display,
loss-based congestion control, and dead-client detection all need those facts, so the
first job is to manufacture them from a tiny header we attach to every packet.

## The pieces

### `PacketHeader` — 12 packed bytes on the front of every packet

```
 0        4        6        8            12
 +--------+--------+--------+------------+
 |protocol|sequence|  ack   |  ack_bits  |
 | id (4) |  (2)   |  (2)   |    (4)     |
 +--------+--------+--------+------------+
```

- **`protocol_id`** — magic number `0x4E455443` ("NETC"). `deserialize()` returns
  `false` if it doesn't match, so stray/foreign datagrams are rejected before we
  touch them. A cheap sanity filter, not security.
- **`sequence`** — our own per-packet counter, incremented on every send.
- **`ack`** — the highest `sequence` we've received *from the peer*.
- **`ack_bits`** — a 32-bit history field. **Bit `n` set ⇒ we also received packet
  `ack − 1 − n`.** So bit 0 = `ack − 1`, bit 1 = `ack − 2`, … bit 31 = `ack − 32`.

The struct is `#pragma pack(push,1)` so there's no padding, and `serialize()` /
`deserialize()` convert each field to/from **network byte order** (`htonl`/`htons`
and back) via `memcpy` into a byte buffer. That byte-order step is what makes the
wire format portable across machines with different endianness — it is not optional.

**Sequence wraparound.** `sequence` is `uint16_t`, so it wraps 65535 → 0. A plain
`s1 > s2` comparison then lies: 0 is *newer* than 65535 but numerically smaller.
`sequence_greater_than()` treats the values as points on a circle and asks whether
the forward distance is the short way round (`≤ 32768`). Getting this wrong works
perfectly for ~18 minutes at 60 Hz and then silently corrupts every ack the instant
the counter wraps — hence a dedicated test.

### `ReliabilitySystem` — acks, RTT, and loss from those headers

This is Glenn Fiedler's sliding-ack-bitfield scheme. One `ack` plus 32 `ack_bits`
means **a single received packet acknowledges up to 33 of the peer's recent
packets.** Because each outgoing header re-states the last 33 acks, an ack is carried
*redundantly* across many packets; you'd have to lose ~33 in a row to lose a specific
packet's acknowledgement. That's why we never send dedicated ACK packets the way TCP
does — the ack piggybacks on whatever we were already sending.

Worked example: we've received the peer's packets **5, 6, 8** but missed **7**. Then
`ack = 8` and, filling `ack_bits` with "bit n = received(ack−1−n)":

- bit 0 → packet 7 → lost → `0`
- bit 1 → packet 6 → received → `1`
- bit 2 → packet 5 → received → `1`

⇒ `ack_bits = 0b110 = 6`. One header tells the peer "your 8, 6, 5 arrived; 7 did
not."

Internally it keeps a fixed-size ring buffer of `PacketRecord{time_sent, acked}`
indexed by sequence (default `buffer_size_ = 1024`), so there is no per-packet
allocation. From the incoming acks it derives:

- **RTT** — every sent packet is time-stamped; when it's acked,
  `sample = (now − time_sent) * 1000`, folded into a smoothed estimate with an
  exponential moving average, `EMA_ALPHA = 0.10`:
  `rtt = (1 − α)·rtt + α·sample`. One outlier can't spike it; the value tracks real
  changes within a few packets. (A sanity clamp rejects samples ≥ 10 s.)
- **packet loss %** — `update()` sweeps a recent window (≤128 packets) and counts,
  among packets old enough that an ack *should* have returned (a dynamic threshold of
  `rtt·1.5 + 50 ms`), the fraction never acked. That raw ratio is itself smoothed
  (`LOSS_EMA_ALPHA = 0.05`) so the displayed loss doesn't flicker.

**It reports; it does not retransmit.** The system exposes an **ack callback**
(`set_ack_callback`) that fires with each newly-acknowledged *packet* sequence.
Higher layers (channels, m03) subscribe to it. The acks are **packet-level, not
message-level**: a packet is a numbered envelope, and which *messages* rode inside it
is tracked one layer up. So the division of labour is:

```
ReliabilitySystem: "packet 42 acked" ──callback──▶ Channel: "42 carried messages
                                                    100–103 → mark delivered,
                                                    stop retransmitting them"
```

That separation lets the identical ack machinery feed a reliable-ordered channel, an
unreliable channel, *and* the network stats without any of them knowing about each
other.

### `Connection` — a session over connectionless UDP

Wraps a `Socket` + a `ReliabilitySystem` and adds the state machine UDP lacks:

```
   Disconnected ──connect()/accept()──▶ Connecting ──first valid reply──▶ Connected
        ▲                                                                     │
        └──────────── timeout, or disconnect() ◀── Disconnecting ◀───────────┘
```

Responsibilities:

- **`connect()` / `accept()`** — start a session as client / server.
- **`send_packet(payload)`** — ask reliability for a fresh header, prepend it, send.
- **`process_packet(datagram)`** — validate the magic, feed the header to reliability
  (updating acks/RTT/loss), flip Connecting → Connected on the first valid reply, and
  hand the raw payload up.
- **`update(current_time)`** — every frame: send a **heartbeat** if we've gone quiet,
  and **time out** the connection if we've heard nothing for too long.

Two timers, both driven from `update()` (constructor defaults):

- **`heartbeat_interval_sec_ = 0.25`** — if we haven't sent for 0.25 s, send an empty
  packet anyway. This keeps ack information flowing and signals "I'm alive" even when
  the game has nothing to say; without it a quiet connection looks dead.
- **`timeout_sec_ = 5.0`** — if we haven't *received* anything for 5 s, declare the
  peer gone and drop to Disconnected.

The ratio is the point: heartbeats every 0.25 s means ~20 keep-alives fit inside one
5 s timeout window, so you must miss many in a row before being dropped — brief
hiccups don't kill a live connection, but a real crash/unplug is caught within
seconds.

At this milestone `Connection` carries **raw payloads only**. Splitting a payload
into typed messages with per-message delivery guarantees is milestone 3 (channels),
which is precisely the layer that consumes the ack callback above.

## What `protocol_test` verifies, and why

1. **Sequence wraparound** — `sequence_greater_than` is correct across the 16-bit
   wrap. *A wrong comparison silently corrupts acks once the counter wraps — invisible
   for ~18 minutes, then reliability breaks.*
2. **Header (de)serialization + magic** — a header survives a byte-for-byte round
   trip and a corrupt/foreign packet is rejected. *The wire format must be exact and
   the magic filter must actually filter.*
3. **Ack bitfield + RTT** — after a simulated exchange the right packets are marked
   acknowledged and RTT comes out ~40 ms (the injected latency). *This is the
   foundation reliability, channels, and every network stat stand on.*

## Run it

```
make test        # adds protocol_test
```

## Mental model

The 12-byte header is a postcard that always says: *"I'm postcard #S — and by the
way, I've received your postcards up to #A, plus these specific earlier ones."* From
the stream of postcards each side works out, from the numbers alone, what got
through, how fast the round trip is, how much is being lost, and whether the other
side is still writing. We rebuilt TCP's useful bookkeeping on UDP and left out the
stalling.
