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

## Milestone 2 - Virtual connection & reliability

- packet_header.go - the 12-byte header (protocol id, sequence, ack, ack-bits),
  serialized explicitly with encoding/binary, plus wrap-aware sequence comparison.
- reliability.go - turns headers into acks, RTT (EMA) and a loss estimate.
- connection.go - the Disconnected/Connecting/Connected state machine with
  heartbeats and timeouts, carrying raw payloads (channels come in m03).
- protocol_test.go - sequence wrap, header round-trip + bad magic, ackBits, RTT
  from acks, and a full loopback handshake.

## Milestone 3 — Channels (delivery guarantees)

Different game data wants different guarantees, so the connection multiplexes three
channels into one UDP datagram, each with its own policy:

- channel.go — the `Channel` interface, the `Message` type, the 5-byte per-message
  wire header (channel id, message id, size), and `ChannelType`.
- channel_unreliable.go — UnreliableUnordered: fire and forget (VoIP, effects).
- channel_sequenced.go — UnreliableSequenced: drop anything older than the newest
  seen (entity positions, camera, analog input).
- channel_reliable.go — ReliableOrdered: retransmit on an RTT timer until acked,
  and buffer out-of-order arrivals to deliver them strictly in order (chat, spawns,
  phase changes).
- connection.go — upgraded with RegisterChannel / SendMessage / ReceiveMessage,
  FlushChannels (pack pending messages behind one reliability header) and
  unpackChannels (route each received slice to its channel). The m02 ack callback is
  fanned out to channels, so ReliableOrdered stops retransmitting once its packet is
  acked.
- channel_test.go — unordered delivery, sequenced drop-older, reliable reassembly,
  reliable ack+retransmit, and a full loopback end-to-end reliable delivery.

Go vs C++ notes: the abstract `Channel` base becomes a Go interface; ReliableOrdered
uses maps (message id -> in-flight message, packet seq -> message ids) instead of
`std::map`, and retransmits lowest ids first via a wrap-aware sort so the receiver
fills its gaps in order.
