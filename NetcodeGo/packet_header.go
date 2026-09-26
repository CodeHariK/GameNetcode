package netcode

import "encoding/binary"

// ProtocolID tags every packet so we can ignore stray / foreign datagrams.
const ProtocolID uint32 = 0x4E455443 // "NETC"

// HeaderSize is the fixed 12-byte wire header on the front of every packet.
const HeaderSize = 12

// PacketHeader is Glenn Fiedler's reliability header:
//
//	protocol_id (4) | sequence (2) | ack (2) | ack_bits (4)
//
// - sequence: our own packet counter (wraps at 16 bits).
// - ack:      the highest sequence we've received from the peer.
// - ackBits:  bit n set => we also received packet (ack - 1 - n).
//
// Unlike the C++ version (which memcpy'd a packed struct), we serialize each
// field explicitly with encoding/binary, so the exact bytes on the wire are
// obvious.
type PacketHeader struct {
	ProtocolID uint32
	Sequence   uint16
	Ack        uint16
	AckBits    uint32
}

// Serialize writes the header as 12 big-endian bytes.
func (h PacketHeader) Serialize() []byte {
	b := make([]byte, HeaderSize)
	binary.BigEndian.PutUint32(b[0:4], h.ProtocolID)
	binary.BigEndian.PutUint16(b[4:6], h.Sequence)
	binary.BigEndian.PutUint16(b[6:8], h.Ack)
	binary.BigEndian.PutUint32(b[8:12], h.AckBits)
	return b
}

// ParseHeader reads a header from the front of a datagram. It returns ok == false
// if the buffer is too short or the protocol id doesn't match.
func ParseHeader(data []byte) (PacketHeader, bool) {
	if len(data) < HeaderSize {
		return PacketHeader{}, false
	}
	h := PacketHeader{
		ProtocolID: binary.BigEndian.Uint32(data[0:4]),
		Sequence:   binary.BigEndian.Uint16(data[4:6]),
		Ack:        binary.BigEndian.Uint16(data[6:8]),
		AckBits:    binary.BigEndian.Uint32(data[8:12]),
	}
	if h.ProtocolID != ProtocolID {
		return PacketHeader{}, false
	}
	return h, true
}

// sequenceGreaterThan reports whether s1 is "newer" than s2, correctly handling
// the 16-bit wraparound (the classic netcode gotcha): the values are close on a
// circle, so "greater" means the forward distance is the short way round.
func sequenceGreaterThan(s1, s2 uint16) bool {
	const half = 1 << 15
	return (s1 > s2 && s1-s2 <= half) || (s1 < s2 && s2-s1 > half)
}
