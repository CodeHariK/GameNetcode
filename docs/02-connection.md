# 02 — Virtual connection & reliability

**Concept.** UDP gives us a way to *send* bytes but nothing else: no connection, no
delivery feedback, no timing, no liveness. This milestone rebuilds exactly those
four things — and *only* those — on top of raw UDP, using a 12-byte header on every
packet. This is where we quietly re-implement the useful half of TCP while keeping
the "drop late data, never stall" behaviour that games need.

## The problem

With raw UDP you call `send(datagram)` and then you know *nothing*:

- Did it arrive? (No ACK comes back.)
- How long did the round trip take? (No timing.)
- Which of the packets *I* sent got through? (No feedback at all.)
- Is the peer even still there? (UDP has no "connection" to drop.)

Every higher-level feature — reliable messages, congestion control, showing the
player their ping, kicking a dead client — needs at least one of those facts. So the
very first thing we build is the machinery that produces them.

The design constraint that shapes everything: **we must never block and never stall
on a lost packet.** TCP's answer to loss is "stop and retransmit, hold everything
behind it until it arrives." For a 60 Hz game that is fatal — a 200 ms-late position
update is worthless; we'd rather have the *next* one. So we do not copy TCP's
delivery model. We copy only its *bookkeeping* (sequence numbers and
acknowledgements) and then make our own decisions about what to do with the
information.

## The header: a 12-byte postcard on every packet

Every single packet — heartbeat, game data, anything — starts with:

```
 0        4        6        8            12   byte offset
 +--------+--------+--------+------------+
 |protocol|sequence|  ack   |  ack_bits  |
 | id (4) |  (2)   |  (2)   |    (4)     |
 +--------+--------+--------+------------+
```

- **`protocol_id`** (4 bytes) — a fixed magic number (`0x4E455443`, ASCII "NETC").
  Any datagram that doesn't begin with it is discarded immediately. This is a cheap
  filter against random internet noise and packets from other programs that happen
  to hit our port; it is *not* security (anyone can copy the constant).
- **`sequence`** (2 bytes) — *our own* packet counter. Every packet we send gets the
  next number. It wraps at 65535 → 0, which matters (see wraparound below).
- **`ack`** (2 bytes) — the highest `sequence` we have received *from them*. "The
  latest thing of yours I've seen is #N."
- **`ack_bits`** (4 bytes) — 32 bits of *history* for the 32 packets before `ack`.
  **Bit `n` set ⇒ "I also received packet `ack − 1 − n`."**

So `sequence` describes *my* stream; `ack` + `ack_bits` describe *what I've heard of
yours*. Both directions ride in the same header, so every packet is simultaneously
new data *and* an acknowledgement of past data.

### Why the bitfield: acks that survive loss

Here is the key trick. One `ack` value acknowledges one packet. The 32 `ack_bits`
acknowledge up to 32 *more*, so **a single received packet acknowledges up to 33 of
the peer's packets.** Because each packet re-states the last 33 acks, an ack is
carried *redundantly* across many packets. For a specific packet's ack to be lost,
you'd have to lose ~33 packets in a row. That is why we don't need to send explicit,
reliable ACK packets the way TCP does — the acknowledgement is piggybacked onto
whatever we were sending anyway, and repeated enough times to shrug off normal loss.

### Worked example

Say peer **A** has been sending; peer **B** wants to tell A what it has received.
B has received A's packets **5, 6, and 8** — but **7 was lost**.

- `remoteSequence` at B = **8** (highest received) ⇒ `ack = 8`.
- Now fill `ack_bits`, where bit `n` = "received packet `ack − 1 − n`":
  - bit 0 → packet `8 − 1 − 0 = 7` → **lost** → `0`
  - bit 1 → packet `8 − 1 − 1 = 6` → received → `1`
  - bit 2 → packet `8 − 1 − 2 = 5` → received → `1`
  - bits 3..31 → older packets, none received here → `0`
- `ack_bits = 0b...00000110 = 6`.

So B sends `ack = 8, ack_bits = 6`. When A parses that it learns, from *one* packet:
"my packets 8, 6, and 5 definitely arrived; 7 did not (yet)." A can now stop
worrying about 5/6/8 and, if 7 carried something important, arrange to resend it.

### Sequence wraparound (the classic bug)

`sequence` is 16-bit, so after 65535 it wraps to 0. Naïve `a > b` comparison then
breaks: is 0 "newer" than 65535? It should be (it's the very next packet), but
numerically it's smaller. The fix is *wrap-aware* comparison — treat the numbers as
points on a circle and ask "is the forward distance the short way round?":

```
newer(s1, s2) := (s1 > s2 && s1 − s2 ≤ 32768) || (s1 < s2 && s2 − s1 > 32768)
```

Get this wrong and everything works fine for the first ~18 minutes at 60 Hz, then
acks silently corrupt themselves the instant the counter wraps. This is why there's
a dedicated test for it.

## Reliability: turning headers into facts

The header is just data on the wire. The *reliability system* is the bookkeeping on
each side that turns a stream of those headers into three useful outputs. It keeps
two small maps bounded to a recent window: `received` (sequences we've seen, used to
build our outgoing `ack_bits`) and `sent` (packets we've sent, awaiting ack, each
stamped with its send time).

**1. Which of my packets got through.** When a header arrives, we read its `ack` and
`ack_bits` and mark each referenced packet in our `sent` map as acknowledged.

**2. RTT (round-trip time).** Each sent packet is time-stamped. The moment it's
acked, `RTT_sample = now − send_time`. We fold that into a smoothed estimate with an
**exponential moving average** so a single weird packet doesn't spike the number:

```
rtt += (sample − rtt) * 0.1
```

The `0.1` is the smoothing factor: each new sample nudges the estimate 10% of the
way toward itself. This is the number you show the player as "ping," and later use to
size interpolation and timeout budgets.

**3. Packet loss %.** Periodically we sweep the `sent` map: of the packets old
enough that an ack *should* have come back by now, what fraction never got acked?
That ratio is the loss estimate — an input to congestion control and diagnostics.

### It reports, it doesn't retransmit — acks are packet-level

Crucial design point: the reliability system **does not resend anything itself.** It
only *reports* "packet N got through" via an **ack callback**. The acks are about
*packets*, not *messages*. A packet is just a numbered envelope; what was *inside*
it (which game messages) is tracked one layer up, by channels (milestone 3). So the
flow is:

```
reliability: "packet 42 was acked" ──callback──▶ channel: "packet 42 carried
                                                  messages 100–103, mark them
                                                  delivered; stop resending them"
```

This separation is what lets the *same* ack machinery power a reliable-ordered
channel, an unreliable channel, and the network stats, without any of them knowing
about each other. The packet layer counts envelopes; the channel layer decides what
"delivered" means for the contents.

## The connection: a session over connectionless UDP

On top of reliability sits a small **state machine** that gives us the notion of a
"connection" UDP lacks:

```
   Disconnected ──connect()/accept()──▶ Connecting ──first valid reply──▶ Connected
        ▲                                                                     │
        └──────────────── timeout, or disconnect() ◀── Disconnecting ◀───────┘
```

- **Disconnected** — nothing going on.
- **Connecting** — a client has sent its first packet(s) and is waiting to hear
  back; a server that has `accept()`ed is waiting for the handshake to complete.
- **Connected** — we've had a valid reply; normal traffic flows.
- **Disconnecting** — an orderly shutdown (send a goodbye, then drop to
  Disconnected).

Two timers keep the session honest, both driven from the per-frame `update()`:

- **Heartbeat (~0.25 s).** If we haven't sent anything for a quarter-second, send an
  empty packet anyway. This keeps `ack` information flowing and tells the peer "I'm
  still here" even when the game has nothing to say. Without it, a quiet connection
  looks dead.
- **Inactivity timeout (~5 s).** If we haven't *received* anything for five seconds,
  declare the peer gone and drop to Disconnected. Since heartbeats arrive every
  0.25 s on a healthy link, five seconds of total silence is a confident "they're
  gone" — a crash, a pulled cable, a closed laptop.

The ratio matters: heartbeat interval ≪ timeout, so a healthy but quiet peer sends
~20 heartbeats inside one timeout window. You need to *miss* many in a row before
being dropped, which keeps brief hiccups from killing a live connection.

The connection's per-frame responsibilities:

- **`connect()` / `accept()`** — begin a session as client / server.
- **`send_packet(payload)`** — ask reliability for a fresh header, prepend it, send.
- **`process_packet(datagram)`** — validate the magic, feed the header to
  reliability (which updates acks/RTT/loss), flip Connecting → Connected on the first
  valid reply, and hand the raw payload upward.
- **`update(dt)`** — send a heartbeat if we've gone quiet; time out the connection if
  we've heard nothing for too long.

At this milestone the connection carries *raw payloads only*. Splitting a payload
into typed messages with different delivery guarantees is the next milestone
(channels).

## What we test (and why)

- **Sequence wraparound** — the "is newer" comparison is correct across the 16-bit
  wrap. *A wrong comparison silently corrupts every ack the moment the counter wraps
  — a bug that hides for ~18 minutes and then ruins reliability.*
- **Header round-trip + magic** — a header serializes and parses back byte-identical,
  and a corrupt/foreign datagram is rejected. *The wire format has to be exact and
  the magic filter has to actually filter.*
- **Ack bitfield + RTT** — after a simulated exchange the right packets are marked
  acknowledged and RTT comes out roughly equal to the injected latency (~40 ms in the
  test). *This is the foundation reliability, channels, and every network stat stand
  on.*

## Mental model

Every packet is a postcard that says, in one breath: *"I'm postcard #S — and by the
way, I've received your postcards up to #A, plus these specific earlier ones."* Each
side reads the incoming stream of postcards and, purely from the numbers, works out
what got through, how fast the round trip is, how much is being lost, and whether the
other side is still writing at all. We rebuilt the useful bookkeeping of TCP on top
of UDP, and left out the part that stalls.

## Code & deep dive

- **C++:** `NetcodeCpp/src/net/{PacketHeader,ReliabilitySystem,Connection}`
  · deep guide `NetcodeCpp/docs/02-connection.md`
  · the header is a packed struct memcpy'd to/from the wire (with byte-order
  conversion).
- **Go:** `NetcodeGo/{packet_header,reliability,connection}.go`
  · the header is serialized field-by-field with `encoding/binary` (big-endian, every
  wire byte explicit) rather than memcpy-ing a struct; non-blocking receive and the
  update loop use Go's `time` (already monotonic) and goroutine/channel idioms.
