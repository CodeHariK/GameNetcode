# 03 — Channels (delivery guarantees)

**Concept.** Different game data needs different guarantees, so the connection
offers several *channels*, multiplexed into one datagram.

## The problem
One-size delivery is wrong: a sound effect can be lost; a position snapshot must
never regress to an older one but a drop is fine; a chat/"player joined" event must
arrive exactly once, in order.

## The idea
Three channels, each with its own policy:
- **UnreliableUnordered** — fire and forget.
- **UnreliableSequenced** — drop anything older than the newest seen (snapshots).
- **ReliableOrdered** — retransmit until acknowledged, and buffer out-of-order
  arrivals to deliver them in order.

Multiple channel messages are packed into a single UDP datagram, each with a small
per-message header (channel id, message id, size). The connection fills the packet
from each channel's queue and, on receipt, routes each slice to its channel.

## What we test (and why)
- Unreliable-sequenced drops an older, out-of-order packet.
- Reliable-ordered reassembles out-of-order arrivals in order.
- Reliable-ordered retransmits an unacked message and then confirms it.

## Code & deep dive
- C++: `NetcodeCpp/src/net/{Channel,ChannelTypes,*Channel}`, `Connection::flush_channels`
  · deep guide `NetcodeCpp/docs/03-channels.md`
- Go: not yet ported.
