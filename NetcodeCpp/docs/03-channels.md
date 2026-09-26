# Milestone 3 — Channels (delivery guarantees)

**What this milestone adds:** milestone 2 gave us one connection with packet-level
acks. Games, though, send data with wildly different needs — some droppable, some
must-arrive, some must-not-regress. This milestone layers three *channels* on top of
the connection, each with its own delivery policy, and multiplexes them into single
UDP datagrams. It is where the m01 promise ("rebuild only the guarantees you need")
becomes concrete and where the m02 ack callback finally gets a consumer.

New files: `src/net/Channel.hpp`, `src/net/ChannelTypes.hpp`,
`src/net/UnreliableUnorderedChannel.hpp`, `src/net/UnreliableSequencedChannel.hpp`,
`src/net/ReliableOrderedChannel.hpp`, `tests/channel_test.cpp`. `Connection` is
upgraded to pack, unpack, and route channel messages.

## The problem

One delivery policy cannot fit all game data:

- A **sound effect / hit spark** — useless if resent late. *Drop it.*
- A **position snapshot** — a drop is fine (the next is ~50 ms away), but applying an
  *older* one after a newer one teleports the entity backward. *Never regress.*
- A **chat / spawn / "player joined" event** — must arrive exactly once, in order, or
  the game state desyncs. *Guarantee and order it.*

TCP would force the third (strict, reliable, ordered) policy onto all three, so hit
sparks and positions inherit head-of-line blocking they never wanted. UDP gives no
guarantees at all. Channels let each message pick its own policy.

## The pieces

### `ChannelTypes` — the message and its wire header
Defines the `Message` (channel id, message id, payload bytes) and the packed 5-byte
`MessageHeader` used when several messages share one datagram:

```
[ channel_id (1) | message_id (2) | payload_size (2) ]  = 5 bytes, then payload
```

`channel_id` routes the slice to a channel on receipt; `message_id` is that channel's
own sequence (unused for the unordered channel); `payload_size` bounds the copy.

### `Channel` — the abstract interface
A base class every policy implements: `send_message` (queue outgoing),
`write_outgoing_messages` (pack pending messages into the packet, respecting the
remaining-byte budget), `process_incoming_message` (accept a received slice),
`receive_message` (pop the next app-deliverable message), `on_packet_acked` (react to
an m02 packet ack), `update` (retransmit timers / RTT propagation), and
`has_outgoing_messages`.

### The three channels

- **`UnreliableUnorderedChannel`** (`ChannelType::UnreliableUnordered`, id 0) — queue,
  send once, forget; `message_id` is always 0. On receipt, straight into the incoming
  queue. Zero tracking, lowest latency.
- **`UnreliableSequencedChannel`** (`ChannelType::UnreliableSequenced`, id 1) — stamps
  each message with an incrementing `local_sequence_`. The receiver keeps
  `remote_sequence_` (the highest seen) and, using `sequence_greater_than` (the same
  wrap-aware compare as the m02 header), **discards anything not strictly newer**,
  bumping a `dropped_count_`. Perfect for snapshots: state never rewinds.
- **`ReliableOrderedChannel`** (`ChannelType::ReliableOrdered`, id 2) — the ENet /
  yojimbo pattern:
  - Sender assigns `local_sequence_` ids and keeps each message in
    `unacked_messages_` (id → `OutgoingMessage{msg, last_sent_time, send_count}`).
  - `write_outgoing_messages` (re)sends a message when it's new (`send_count == 0`) or
    its retransmit timer has elapsed: `rto = max(50 ms, (rtt_ms + 25) / 1000)` — about
    a round trip plus margin. Each time it packs a message, it records
    `packet_to_messages_[current_packet_sequence_].push_back(msg_id)`.
  - `on_packet_acked(seq)` (driven by the m02 ack callback) looks up
    `packet_to_messages_[seq]` and **erases those message ids** from
    `unacked_messages_` — they're confirmed, stop resending. **Acks are packet-level;
    this map turns them back into message-level confirmations.**
  - The receiver buffers arrivals in `reassembly_buffer_` (id → Message) and
    `receive_message` releases them only in contiguous order via
    `expected_receive_sequence_` — **head-of-line handling, isolated to this channel.**

### `Connection` upgrade — multiplexing

- **`setup_callbacks()`** wires `reliability_.set_ack_callback` to fan every packet ack
  out to all channels' `on_packet_acked` — this is the m02 seam being consumed.
- **`flush_channels()`** — if any channel has pending data, it generates one packet
  header, tags every `ReliableOrderedChannel` with that header's sequence
  (`set_current_packet_sequence`), then walks the channels writing as many messages as
  fit into a `MAX_PACKET_SIZE = 1200` buffer (after the 12-byte header), and sends the
  one datagram. One reliability header amortized over many messages, never fragmented.
- **`process_packet()`** — after feeding the header to reliability, it walks the
  payload as a run of 5-byte-headed slices, routing each to `get_channel(ch_id)->
  process_incoming_message(...)`; a malformed/oversized slice stops the unpack. A
  packet with **no** channel messages (a bare heartbeat) is surfaced as a raw payload,
  exactly as in m02.
- **`send_message` / `receive_message`** — the high-level API: enqueue on a channel by
  id; receive by polling all channels for the next deliverable message.

A retransmitted reliable message rides a *new* packet with a *new* sequence, so one
message id can appear under several entries in `packet_to_messages_`; an ack of any of
them confirms it.

## What `channel_test` verifies, and why

1. **Unreliable unordered** delivers what you send. *Baseline pack/unpack round trip.*
2. **Unreliable sequenced** drops an out-of-order (older) message. *Snapshots must
   never regress to a stale state.*
3. **Reliable ordered reassembly** — scrambled arrivals are delivered to the app in
   order, and a gap holds back everything behind it. *Chat / events must not scramble.*
4. **Reliable ordered ack + retransmit** — an unacked message is resent, then cleared
   once acked. *Reliability must recover losses and then stop.*

## Run it

```
make test        # adds channel_test
```

## Mental model

One UDP datagram is a shipping container; channels are mail classes sharing it: junk
mail (drop freely), "latest photo only" (bin older prints), and registered mail (keep
resending until signed for, deliver in order). The connection packs one container from
all three queues and, at the far end, sorts each envelope back to its class.
