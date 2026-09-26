package netcode

import "time"

// UnreliableUnorderedChannel sends each message once. No sequencing, no ordering, no
// retransmission — the cheapest, lowest-latency policy. A dropped message is simply
// gone; a late one is delivered whenever it turns up.
type UnreliableUnorderedChannel struct {
	id       uint8
	outgoing [][]byte  // pending payloads, FIFO
	incoming []Message // delivered-but-not-yet-consumed messages
}

func NewUnreliableUnorderedChannel(id uint8) *UnreliableUnorderedChannel {
	return &UnreliableUnorderedChannel{id: id}
}

func (c *UnreliableUnorderedChannel) ChannelID() uint8  { return c.id }
func (c *UnreliableUnorderedChannel) Type() ChannelType { return ChannelUnreliableUnordered }
func (c *UnreliableUnorderedChannel) HasOutgoing() bool { return len(c.outgoing) > 0 }

func (c *UnreliableUnorderedChannel) SendMessage(data []byte, _ time.Time) {
	c.outgoing = append(c.outgoing, append([]byte(nil), data...)) // copy: caller may reuse data
}

func (c *UnreliableUnorderedChannel) WriteOutgoing(dst []byte, _ time.Time) int {
	written := 0
	for len(c.outgoing) > 0 {
		n := writeMessage(dst[written:], c.id, 0, c.outgoing[0]) // message id unused when unordered
		if n == 0 {
			break // remaining packet space exhausted
		}
		written += n
		c.outgoing = c.outgoing[1:]
	}
	return written
}

func (c *UnreliableUnorderedChannel) ProcessIncoming(messageID uint16, payload []byte) bool {
	c.incoming = append(c.incoming, Message{
		ChannelID: c.id,
		MessageID: messageID,
		Payload:   append([]byte(nil), payload...),
	})
	return true
}

func (c *UnreliableUnorderedChannel) ReceiveMessage() (Message, bool) {
	if len(c.incoming) == 0 {
		return Message{}, false
	}
	m := c.incoming[0]
	c.incoming = c.incoming[1:]
	return m, true
}

func (c *UnreliableUnorderedChannel) OnPacketAcked(uint16)      {}
func (c *UnreliableUnorderedChannel) Update(time.Time, float64) {}
