package netcode

import (
	"encoding/binary"
	"net/netip"
	"time"
)

// ConnectionState is the virtual-session state machine over connectionless UDP.
type ConnectionState int

const (
	Disconnected ConnectionState = iota
	Connecting
	Connected
	Disconnecting
)

func (s ConnectionState) String() string {
	switch s {
	case Disconnected:
		return "Disconnected"
	case Connecting:
		return "Connecting"
	case Connected:
		return "Connected"
	case Disconnecting:
		return "Disconnecting"
	}
	return "Unknown"
}

// maxPacketSize is a conservative game MTU: one datagram never exceeds this, so we
// stay under path-MTU limits and avoid IP fragmentation.
const maxPacketSize = 1200

// Connection turns raw UDP into a session: a state machine, keep-alive heartbeats, an
// inactivity timeout, packet-level reliability (ReliabilitySystem), and — from m03 —
// a set of delivery channels multiplexed into single datagrams.
type Connection struct {
	socket    *Socket
	remote    netip.AddrPort
	state     ConnectionState
	timeout   time.Duration
	heartbeat time.Duration
	lastSent  time.Time
	lastRecv  time.Time
	rel       *ReliabilitySystem
	channels  map[uint8]Channel
}

func NewConnection(socket *Socket, timeout, heartbeat time.Duration) *Connection {
	c := &Connection{
		socket:    socket,
		timeout:   timeout,
		heartbeat: heartbeat,
		rel:       NewReliabilitySystem(),
		channels:  make(map[uint8]Channel),
	}
	// When the reliability layer acks a packet, tell every channel: reliable channels
	// use it to stop retransmitting the messages that packet carried.
	c.rel.SetAckCallback(func(seq uint16) {
		for _, ch := range c.channels {
			ch.OnPacketAcked(seq)
		}
	})
	return c
}

func (c *Connection) State() ConnectionState          { return c.state }
func (c *Connection) IsConnected() bool               { return c.state == Connected }
func (c *Connection) Remote() netip.AddrPort          { return c.remote }
func (c *Connection) RTTms() float64                  { return c.rel.RTTms() }
func (c *Connection) PacketLoss() float64             { return c.rel.PacketLoss() }
func (c *Connection) Reliability() *ReliabilitySystem { return c.rel }

// RegisterChannel adds a delivery channel, keyed by its wire id. Register the same
// ids on both peers so messages route to the matching policy on each side.
func (c *Connection) RegisterChannel(ch Channel) { c.channels[ch.ChannelID()] = ch }

// Channel returns a registered channel by id, or nil.
func (c *Connection) Channel(id uint8) Channel { return c.channels[id] }

// Connect starts a session as the client (we reach out to a server).
func (c *Connection) Connect(addr netip.AddrPort, now time.Time) {
	c.remote = addr
	c.state = Connecting
	c.lastRecv = now
	c.rel.Reset()
}

// Accept starts a session as the server (we take an incoming client).
func (c *Connection) Accept(addr netip.AddrPort, now time.Time) {
	c.remote = addr
	c.state = Connected
	c.lastSent = now
	c.lastRecv = now
	c.rel.Reset()
}

func (c *Connection) Disconnect() {
	c.state = Disconnected
	c.rel.Reset()
}

// SendMessage queues application data on a channel. Returns false if the id is
// unknown. The bytes go out on the next flush (Update, or FlushChannels).
func (c *Connection) SendMessage(channelID uint8, data []byte, now time.Time) bool {
	ch, ok := c.channels[channelID]
	if !ok {
		return false
	}
	ch.SendMessage(data, now)
	return true
}

// ReceiveMessage pops the next app-deliverable message from any channel.
func (c *Connection) ReceiveMessage() (Message, bool) {
	for _, ch := range c.channels {
		if m, ok := ch.ReceiveMessage(); ok {
			return m, true
		}
	}
	return Message{}, false
}

// SendPacket wraps a raw payload in a reliability header and sends it. Used for
// heartbeats (nil payload) and any non-channel raw data.
func (c *Connection) SendPacket(payload []byte, now time.Time) error {
	if c.state == Disconnected {
		return nil
	}
	h := c.rel.GenerateHeader(now)
	packet := append(h.Serialize(), payload...)
	err := c.socket.Send(c.remote, packet)
	if err == nil {
		c.lastSent = now
	}
	return err
}

// FlushChannels packs pending messages from every channel into one datagram (behind a
// fresh reliability header) and sends it. It is a no-op when nothing is pending.
func (c *Connection) FlushChannels(now time.Time) error {
	if c.state == Disconnected {
		return nil
	}
	hasData := false
	for _, ch := range c.channels {
		if ch.HasOutgoing() {
			hasData = true
			break
		}
	}
	if !hasData {
		return nil
	}

	h := c.rel.GenerateHeader(now)
	buf := make([]byte, maxPacketSize)
	copy(buf, h.Serialize())

	// Tag reliable channels with this packet's sequence before packing, so their acks
	// can be mapped back to the messages we're about to write.
	for _, ch := range c.channels {
		if rc, ok := ch.(*ReliableOrderedChannel); ok {
			rc.SetCurrentPacketSequence(h.Sequence)
		}
	}

	off := HeaderSize
	for _, ch := range c.channels {
		if off >= maxPacketSize {
			break
		}
		off += ch.WriteOutgoing(buf[off:], now)
	}
	if off == HeaderSize {
		return nil // nothing actually fit / packed
	}

	err := c.socket.Send(c.remote, buf[:off])
	if err == nil {
		c.lastSent = now
	}
	return err
}

// ProcessPacket validates an incoming datagram, feeds its header to reliability, and
// unpacks any multiplexed channel messages. If the body held channel messages they
// are routed to their channels and payload is nil; otherwise the raw body is returned
// (e.g. a bare heartbeat, or a raw SendPacket payload). ok == false means not for us.
func (c *Connection) ProcessPacket(from netip.AddrPort, data []byte, now time.Time) (payload []byte, ok bool) {
	if len(data) < HeaderSize {
		return nil, false
	}
	if c.state != Disconnected && from != c.remote {
		return nil, false
	}
	h, valid := ParseHeader(data)
	if !valid {
		return nil, false
	}
	c.rel.PacketReceived(h, now)
	c.lastRecv = now
	if c.state == Connecting {
		c.state = Connected
	}

	body := data[HeaderSize:]
	if c.unpackChannels(body) {
		return nil, true
	}
	return body, true
}

// unpackChannels splits a packet body into per-message slices and routes each to its
// channel. Returns true if at least one channel message was parsed.
func (c *Connection) unpackChannels(body []byte) bool {
	parsed := false
	off := 0
	for off+MessageHeaderSize <= len(body) {
		chID := body[off]
		msgID := binary.BigEndian.Uint16(body[off+1 : off+3])
		msgLen := int(binary.BigEndian.Uint16(body[off+3 : off+5]))
		if off+MessageHeaderSize+msgLen > len(body) {
			break // malformed / truncated slice, stop unpacking
		}
		if ch, ok := c.channels[chID]; ok {
			ch.ProcessIncoming(msgID, body[off+MessageHeaderSize:off+MessageHeaderSize+msgLen])
			parsed = true
		}
		off += MessageHeaderSize + msgLen
	}
	return parsed
}

// Update handles timeouts, channel flushing, keep-alive heartbeats, and per-frame
// housekeeping; call it every frame.
func (c *Connection) Update(now time.Time) {
	if c.state == Disconnected {
		return
	}
	if now.Sub(c.lastRecv) > c.timeout {
		c.Disconnect()
		return
	}
	c.FlushChannels(now)
	if now.Sub(c.lastSent) >= c.heartbeat {
		_ = c.SendPacket(nil, now) // empty keep-alive
	}
	for _, ch := range c.channels {
		ch.Update(now, c.rel.RTTms())
	}
	c.rel.Update(now)
}
