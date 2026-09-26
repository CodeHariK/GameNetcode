# Milestone 3 — Channels (delivery guarantees)

**What this milestone adds:** different kinds of game data need different
guarantees, so we layer three *channels* on top of the connection and multiplex
them into a single UDP datagram.

New files: `src/net/Channel`, `src/net/ChannelTypes`,
`src/net/UnreliableUnorderedChannel`, `src/net/UnreliableSequencedChannel`,
`src/net/ReliableOrderedChannel`, `tests/channel_test.cpp`. `Connection` is
upgraded to pack/unpack channel messages.

## The problem

One-size-fits-all delivery is wrong for games:
- A **sound effect** can be lost — resending it late is useless.
- A **position snapshot** must never apply an *old* one after a newer one, but a
  dropped one doesn't matter (the next is coming in 50 ms).
- A **chat message / "player joined"** must arrive, exactly once, in order.

So the connection offers three channels, each with its own policy.

## The pieces

### `ChannelTypes` — the message + wire header
Defines `Message` (channel id, message id, payload bytes) and the 5-byte per-message
header (`channel_id`, `message_id`, `payload_size`) used when several messages are
packed into one datagram.

### `Channel` — the interface
An abstract base: `send_message`, `write_outgoing_messages` (pack pending messages
into the packet, respecting the remaining space), `process_incoming_message`,
`receive_message`, `on_packet_acked`, `update`.

### The three channels
- **UnreliableUnordered** (`CH_UNRELIABLE`, id 0) — queue and send; no tracking.
- **UnreliableSequenced** (`CH_STATE`, id 1) — stamps a sequence; the receiver
  drops anything older than the newest it has seen (perfect for snapshots).
- **ReliableOrdered** (`CH_RELIABLE`, id 2) — keeps unacked messages and
  **retransmits** them (using the m02 ack callback) until confirmed, and the
  receiver **buffers out-of-order arrivals** and releases them in order
  (head-of-line handling). Used for chat, spawn/despawn, and later fire/hit events.

### `Connection` upgrade — multiplexing
`flush_channels()` walks the channels and packs as many pending messages as fit
into one datagram (after the 12-byte reliability header). `process_packet()` now
unpacks those per-message slices and routes each to its channel; a packet with no
channel messages (e.g. a bare heartbeat) is still handed up as a raw payload.

## What `channel_test` verifies, and why

1. **Unreliable unordered** delivers what you send. *Baseline.*
2. **Unreliable sequenced** drops an out-of-order (older) packet. *Snapshots must
   never regress to a stale state.*
3. **Reliable ordered reassembly** — messages that arrive out of order are
   delivered to the app in order. *Chat/events must not scramble.*
4. **Reliable ordered ack + retransmit** — an unacked message is resent and then
   confirmed once acked. *Reliability must actually recover losses.*

## Run it

```
make test        # adds channel_test
```

## Mental model

One UDP datagram is a shipping container. Channels are different mail classes
sharing it: junk mail (drop freely), "latest photo only" (toss older ones), and
registered mail (keep resending until signed for, deliver in order).
