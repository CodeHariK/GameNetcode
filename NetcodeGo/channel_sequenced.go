package netcode

import "time"

// UnreliableSequencedChannel stamps each message with a 16-bit sequence. Delivery is
// still unreliable (no retransmission), but the receiver discards any message older
// than the newest it has already accepted — so state never regresses to a stale value.
// This is the right policy for entity positions: a dropped snapshot doesn't matter
// (the next is coming), but applying an old one after a newer one would visibly rewind.
type UnreliableSequencedChannel struct {
	id             uint8
	localSequence  uint16
	remoteSequence uint16
	hasRemote      bool
	dropped        uint64
	outgoing       []Message
	incoming       []Message
}

func NewUnreliableSequencedChannel(id uint8) *UnreliableSequencedChannel {
	return &UnreliableSequencedChannel{id: id}
}

func (c *UnreliableSequencedChannel) ChannelID() uint8  { return c.id }
func (c *UnreliableSequencedChannel) Type() ChannelType { return ChannelUnreliableSequenced }
func (c *UnreliableSequencedChannel) HasOutgoing() bool { return len(c.outgoing) > 0 }
func (c *UnreliableSequencedChannel) Dropped() uint64   { return c.dropped }

func (c *UnreliableSequencedChannel) SendMessage(data []byte, _ time.Time) {
	c.outgoing = append(c.outgoing, Message{
		ChannelID: c.id,
		MessageID: c.localSequence,
		Payload:   append([]byte(nil), data...),
	})
	c.localSequence++
}

func (c *UnreliableSequencedChannel) WriteOutgoing(dst []byte, _ time.Time) int {
	written := 0
	for len(c.outgoing) > 0 {
		m := c.outgoing[0]
		n := writeMessage(dst[written:], c.id, m.MessageID, m.Payload)
		if n == 0 {
			break // remaining packet space exhausted
		}
		written += n
		c.outgoing = c.outgoing[1:]
	}
	return written
}

func (c *UnreliableSequencedChannel) ProcessIncoming(messageID uint16, payload []byte) bool {
	if c.hasRemote && !sequenceGreaterThan(messageID, c.remoteSequence) {
		c.dropped++
		return false // older than / equal to newest seen -> drop
	}
	c.remoteSequence = messageID
	c.hasRemote = true
	c.incoming = append(c.incoming, Message{
		ChannelID: c.id,
		MessageID: messageID,
		Payload:   append([]byte(nil), payload...),
	})
	return true
}

func (c *UnreliableSequencedChannel) ReceiveMessage() (Message, bool) {
	if len(c.incoming) == 0 {
		return Message{}, false
	}
	m := c.incoming[0]
	c.incoming = c.incoming[1:]
	return m, true
}

func (c *UnreliableSequencedChannel) OnPacketAcked(uint16)      {}
func (c *UnreliableSequencedChannel) Update(time.Time, float64) {}
