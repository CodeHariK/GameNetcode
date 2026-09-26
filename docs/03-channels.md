# 03 — Channels (delivery guarantees)

**Concept.** Milestone 2 gave us one pipe with packet-level acks. But a game sends
wildly different kinds of data down that pipe, and each kind wants a *different*
delivery guarantee. So we layer several **channels** on top of the connection, each
with its own policy, and multiplex them into a single UDP datagram. This is where the
"rebuild only the guarantees you need" promise from milestone 1 finally pays off:
instead of TCP's one blunt policy for everything, each data type gets exactly the
policy it wants.

## The problem

One-size-fits-all delivery is wrong for games. Consider three messages:

- A **sound effect / hit spark** — if it's lost, resending it 100 ms late is
  useless; the moment has passed. *Drop it and move on.*
- A **position snapshot** — a dropped one doesn't matter (the next arrives in ~50 ms),
  but applying an *old* one after a newer one makes the entity visibly teleport
  backward. *Never regress to a stale value; a gap is fine.*
- A **chat message / "player joined" / spawn event** — must arrive, exactly once, in
  order. Losing it desyncs the game state. *Guarantee it, and keep the order.*

TCP gives the third policy to *all three*, so your hit sparks and positions inherit
head-of-line blocking they never needed — a single lost snapshot would stall the chat
*and* the next position. UDP gives you nothing, which is worse. The right answer is
per-message choice, and that's what channels provide.

## The idea

Three channels, each a small state machine with its own policy:

| Channel | Policy | Loss | Order | Use for |
|---|---|---|---|---|
| **UnreliableUnordered** | fire and forget | tolerated | none | VoIP, ambient FX, throwaway stats |
| **UnreliableSequenced** | keep only the newest | tolerated | newest-wins | positions, camera, analog input |
| **ReliableOrdered** | retransmit + reassemble | recovered | strict | chat, spawn/despawn, phase changes, hits |

The three differ only in what they *remember* and what they *do on receipt*:

- **UnreliableUnordered** — queue outgoing, send once, forget. On receipt, hand it
  straight up. No sequence, no buffer, zero overhead.
- **UnreliableSequenced** — stamp each message with a 16-bit sequence. The receiver
  keeps only the highest sequence it has seen and **discards anything older or equal**
  (wrap-aware comparison, same trick as the packet header in m02). A late snapshot is
  simply dropped, so state never rewinds.
- **ReliableOrdered** — the expensive one, and the most interesting:
  - The sender assigns each message a 16-bit id and **keeps a copy** until it's
    confirmed. Unacked messages are **retransmitted** on an RTT-based timer.
  - Confirmation reuses the m02 machinery: when the reliability system reports "packet
    N was acked" (the ack callback), the channel looks up which message ids rode in
    packet N and drops them from its retransmit set. **Acks are packet-level; the
    channel maps them back to messages.** (This is the exact seam the m02 docs
    described — this milestone is what plugs into that callback.)
  - The receiver **buffers out-of-order arrivals** in a reassembly map and releases
    them only as the contiguous next-expected id becomes available (**head-of-line**
    handling). Crucially this blocking is *isolated per channel*: a stalled reliable
    stream never holds up the unreliable channels sharing the same packets.

### Multiplexing: many messages, one datagram

Sending each message as its own UDP packet would waste the 12-byte reliability header
over and over and flood the socket. Instead, one datagram carries the reliability
header once, followed by a run of messages, each prefixed with a **5-byte per-message
header**:

```
 +----------------- one UDP datagram -----------------+
 | reliability header (12) |  msg | msg | msg | ...   |
 +----------------+--------------------+--------------+
                  each msg = [ channel_id(1) | message_id(2) | size(2) | payload ]
```

On send, the connection asks each channel to pack as many pending messages as fit
into the remaining space (a ~1200-byte MTU budget, so we never fragment). On receipt,
it walks those 5-byte headers, slices out each payload, and routes it to the channel
named by `channel_id`. A packet with no channel messages (a bare heartbeat) is just
handed up as a raw payload, exactly as in m02.

Two subtleties worth noting: the reliable channel is told *which packet sequence* it's
being packed into just before writing, so it can record the message-id → packet-seq
mapping the ack callback later needs; and because a retransmitted message rides in a
*new* packet with a *new* sequence, the same message id can map to several packet
sequences over its life — any one of them being acked confirms it.

## What we test (and why)

- **Unreliable-unordered delivers** what you send. *Baseline: packing and unpacking
  round-trips correctly.*
- **Unreliable-sequenced drops an older arrival.** *Snapshots must never regress to a
  stale state — the whole reason this channel exists.*
- **Reliable-ordered reassembly** — messages that arrive scrambled are delivered to
  the app in order, and a gap holds back everything behind it until filled. *Chat and
  events must not scramble or skip.*
- **Reliable-ordered ack + retransmit** — an unacked message is resent after the RTO,
  then cleared once its packet is acked. *Reliability must actually recover losses and
  then stop, not resend forever.*

## Mental model

One UDP datagram is a shipping container, and channels are different mail classes
sharing it: **junk mail** (UnreliableUnordered — toss it if it's lost), **"latest
photo only"** (UnreliableSequenced — bin any older print the moment a newer one
arrives), and **registered mail** (ReliableOrdered — keep resending until it's signed
for, and deliver the batch strictly in order). The postal clerk (the connection)
packs one container from all three queues and, at the far end, sorts each envelope
back to its class.

## Code & deep dive

- **C++:** `NetcodeCpp/src/net/{Channel,ChannelTypes,*Channel}`, `Connection::flush_channels`
  · deep guide `NetcodeCpp/docs/03-channels.md`
  · channels are subclasses of an abstract `Channel`; the reliable channel uses
  `std::map` for its unacked / packet-to-messages / reassembly tables.
- **Go:** `NetcodeGo/{channel,channel_unreliable,channel_sequenced,channel_reliable}.go`,
  `Connection.FlushChannels` / `Connection.unpackChannels` (multiplexing)
  · notes: the `Channel` interface mirrors the C++ abstract base; per-message headers
  use `encoding/binary` (big-endian), and the reliable channel tracks in-flight
  messages in maps keyed by message id and by carrying-packet sequence, retransmitting
  lowest ids first via a wrap-aware sort so the receiver fills its gaps in order.
