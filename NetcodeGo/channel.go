package netcode

import (
	"encoding/binary"
	"time"
)

// ChannelType is the delivery policy a channel implements. Different game data wants
// different guarantees, so a connection multiplexes several channels into one UDP
// datagram and each picks its own policy.
type ChannelType uint8

const (
	// ChannelUnreliableUnordered: fire-and-forget. No sequencing, no retransmit.
	// Best for VoIP, ambient effects, high-frequency throwaway stats.
	ChannelUnreliableUnordered ChannelType = iota
	// ChannelUnreliableSequenced: unreliable, but the receiver drops anything older
	// than the newest it has seen. Best for entity positions, camera, analog input.
	ChannelUnreliableSequenced
	// ChannelReliableOrdered: guaranteed delivery, strict in-order reassembly.
	// Retransmitted until acked; receiver buffers out-of-order arrivals.
	// Best for chat, spawn/despawn, phase transitions, inventory actions.
	ChannelReliableOrdered
)

func (t ChannelType) String() string {
	switch t {
	case ChannelUnreliableUnordered:
		return "UnreliableUnordered"
	case ChannelUnreliableSequenced:
		return "UnreliableSequenced"
	case ChannelReliableOrdered:
		return "ReliableOrdered"
	}
	return "Unknown"
}

// MessageHeaderSize is the per-message wire prefix packed ahead of each payload when
// several messages share one UDP datagram:
//
//	channel_id (1) | message_id (2) | payload_size (2)  = 5 bytes
const MessageHeaderSize = 5

// Message is one unit of application data delivered over a channel.
type Message struct {
	ChannelID uint8
	MessageID uint16
	Payload   []byte
}

// writeMessage packs [header | payload] into dst (the remaining packet space) and
// returns the bytes written, or 0 if it does not fit. Message ids and lengths are
// big-endian, matching the C++ wire format.
func writeMessage(dst []byte, channelID uint8, messageID uint16, payload []byte) int {
	total := MessageHeaderSize + len(payload)
	if total > len(dst) {
		return 0
	}
	dst[0] = channelID
	binary.BigEndian.PutUint16(dst[1:3], messageID)
	binary.BigEndian.PutUint16(dst[3:5], uint16(len(payload)))
	copy(dst[MessageHeaderSize:], payload)
	return total
}

// Channel is one delivery policy layered on a Connection. Every message carries a
// header saying which channel and message id it is, so the receiver routes it back to
// the matching channel.
type Channel interface {
	// SendMessage queues application data for transmission.
	SendMessage(data []byte, now time.Time)
	// WriteOutgoing packs pending messages into dst (the remaining packet space) and
	// returns bytes written. It must never exceed len(dst).
	WriteOutgoing(dst []byte, now time.Time) int
	// ProcessIncoming handles one received message slice for this channel.
	ProcessIncoming(messageID uint16, payload []byte) bool
	// ReceiveMessage pops the next app-deliverable message, if any.
	ReceiveMessage() (Message, bool)
	// OnPacketAcked is called when the ReliabilitySystem acks a packet sequence.
	OnPacketAcked(packetSeq uint16)
	// Update runs per-frame housekeeping (retransmit timers, RTT propagation).
	Update(now time.Time, rttMs float64)
	// HasOutgoing reports whether messages are waiting to be sent.
	HasOutgoing() bool
	// ChannelID is this channel's wire id.
	ChannelID() uint8
	// Type is the delivery policy.
	Type() ChannelType
}
