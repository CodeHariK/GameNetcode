package netcode

import (
	"sort"
	"time"
)

// ReliableOrderedChannel guarantees delivery and strict in-order reassembly
// (the ENet / yojimbo pattern), built on top of the m02 packet-level acks:
//
//  1. Each message gets a 16-bit message id.
//  2. Unacked messages are kept and retransmitted on an RTT-based timer until the
//     ReliabilitySystem confirms the packet that carried them (OnPacketAcked).
//  3. The receiver buffers out-of-order arrivals and releases them only as the
//     contiguous next-expected id becomes available (head-of-line, isolated per
//     channel so a stall here never blocks the unreliable channels).
type ReliableOrderedChannel struct {
	id uint8

	localSequence           uint16
	expectedReceiveSequence uint16
	currentPacketSequence   uint16
	hasPacketContext        bool
	rttMs                   float64

	unacked          map[uint16]*outgoingMessage // message id -> in-flight message
	packetToMessages map[uint16][]uint16         // packet seq -> message ids it carried
	reassembly       map[uint16]Message          // out-of-order receive buffer
}

type outgoingMessage struct {
	msg          Message
	lastSentTime time.Time
	sendCount    int
}

func NewReliableOrderedChannel(id uint8) *ReliableOrderedChannel {
	return &ReliableOrderedChannel{
		id:               id,
		rttMs:            40.0, // reasonable default until the first ack measures RTT
		unacked:          make(map[uint16]*outgoingMessage),
		packetToMessages: make(map[uint16][]uint16),
		reassembly:       make(map[uint16]Message),
	}
}

func (c *ReliableOrderedChannel) ChannelID() uint8        { return c.id }
func (c *ReliableOrderedChannel) Type() ChannelType       { return ChannelReliableOrdered }
func (c *ReliableOrderedChannel) HasOutgoing() bool       { return len(c.unacked) > 0 }
func (c *ReliableOrderedChannel) PendingUnacked() int     { return len(c.unacked) }
func (c *ReliableOrderedChannel) ReassemblyBuffered() int { return len(c.reassembly) }

// SetCurrentPacketSequence tells the channel which UDP packet sequence the next
// WriteOutgoing call is being packed into, so acks of that packet can be mapped back
// to the messages it carried. The Connection sets this just before flushing.
func (c *ReliableOrderedChannel) SetCurrentPacketSequence(seq uint16) {
	c.currentPacketSequence = seq
	c.hasPacketContext = true
}

func (c *ReliableOrderedChannel) SendMessage(data []byte, _ time.Time) {
	id := c.localSequence
	c.localSequence++
	c.unacked[id] = &outgoingMessage{
		msg: Message{ChannelID: c.id, MessageID: id, Payload: append([]byte(nil), data...)},
	}
}

func (c *ReliableOrderedChannel) WriteOutgoing(dst []byte, now time.Time) int {
	rto := c.retransmitInterval()
	written := 0

	// Send lowest message ids first so the receiver fills its gaps in order.
	ids := make([]uint16, 0, len(c.unacked))
	for id := range c.unacked {
		ids = append(ids, id)
	}
	sort.Slice(ids, func(i, j int) bool { return sequenceGreaterThan(ids[j], ids[i]) })

	for _, id := range ids {
		o := c.unacked[id]
		if o.sendCount > 0 && now.Sub(o.lastSentTime) < rto {
			continue // sent recently, not yet due for retransmission
		}
		n := writeMessage(dst[written:], c.id, o.msg.MessageID, o.msg.Payload)
		if n == 0 {
			break // remaining packet space exhausted
		}
		written += n
		o.lastSentTime = now
		o.sendCount++
		if c.hasPacketContext {
			seq := c.currentPacketSequence
			c.packetToMessages[seq] = append(c.packetToMessages[seq], id)
		}
	}
	return written
}

// retransmitInterval = smoothed RTT + 25ms margin, floored at 50ms, so we wait about
// a round trip for an ack before assuming loss.
func (c *ReliableOrderedChannel) retransmitInterval() time.Duration {
	ms := c.rttMs + 25.0
	if ms < 50.0 {
		ms = 50.0
	}
	return time.Duration(ms * float64(time.Millisecond))
}

func (c *ReliableOrderedChannel) OnPacketAcked(packetSeq uint16) {
	ids, ok := c.packetToMessages[packetSeq]
	if !ok {
		return
	}
	for _, id := range ids {
		delete(c.unacked, id) // confirmed delivered -> stop retransmitting
	}
	delete(c.packetToMessages, packetSeq)
}

func (c *ReliableOrderedChannel) ProcessIncoming(messageID uint16, payload []byte) bool {
	// Ignore anything at or below what we've already delivered (duplicate / stale).
	if messageID != c.expectedReceiveSequence && !sequenceGreaterThan(messageID, c.expectedReceiveSequence) {
		return false
	}
	if _, exists := c.reassembly[messageID]; !exists {
		c.reassembly[messageID] = Message{
			ChannelID: c.id,
			MessageID: messageID,
			Payload:   append([]byte(nil), payload...),
		}
	}
	return true
}

func (c *ReliableOrderedChannel) ReceiveMessage() (Message, bool) {
	m, ok := c.reassembly[c.expectedReceiveSequence]
	if !ok {
		return Message{}, false // next-in-order id hasn't arrived yet -> hold the line
	}
	delete(c.reassembly, c.expectedReceiveSequence)
	c.expectedReceiveSequence++
	return m, true
}

func (c *ReliableOrderedChannel) Update(_ time.Time, rttMs float64) {
	c.rttMs = rttMs
}
