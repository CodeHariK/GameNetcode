package netcode

import (
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

// Connection turns raw UDP into a session: a state machine, keep-alive heartbeats,
// an inactivity timeout, and packet-level reliability (via ReliabilitySystem).
//
// Milestone 2: it carries raw payloads only. Channels (m03) and the network
// simulator hook (m09) are layered on later.
type Connection struct {
	socket    *Socket
	remote    netip.AddrPort
	state     ConnectionState
	timeout   time.Duration
	heartbeat time.Duration
	lastSent  time.Time
	lastRecv  time.Time
	rel       *ReliabilitySystem
}

func NewConnection(socket *Socket, timeout, heartbeat time.Duration) *Connection {
	return &Connection{
		socket:    socket,
		timeout:   timeout,
		heartbeat: heartbeat,
		rel:       NewReliabilitySystem(),
	}
}

func (c *Connection) State() ConnectionState          { return c.state }
func (c *Connection) IsConnected() bool               { return c.state == Connected }
func (c *Connection) Remote() netip.AddrPort          { return c.remote }
func (c *Connection) RTTms() float64                  { return c.rel.RTTms() }
func (c *Connection) PacketLoss() float64             { return c.rel.PacketLoss() }
func (c *Connection) Reliability() *ReliabilitySystem { return c.rel }

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

// SendPacket wraps a payload in a reliability header and sends it.
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

// ProcessPacket validates and consumes an incoming datagram, returning the raw
// payload after the header. ok == false means the packet was not for us.
func (c *Connection) ProcessPacket(from netip.AddrPort, data []byte, now time.Time) (payload []byte, ok bool) {
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
	return data[HeaderSize:], true
}

// Update handles timeouts and keep-alive heartbeats; call it every frame.
func (c *Connection) Update(now time.Time) {
	if c.state == Disconnected {
		return
	}
	if now.Sub(c.lastRecv) > c.timeout {
		c.Disconnect()
		return
	}
	if now.Sub(c.lastSent) >= c.heartbeat {
		_ = c.SendPacket(nil, now) // empty keep-alive
	}
	c.rel.Update(now)
}
