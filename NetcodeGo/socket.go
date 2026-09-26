package netcode

import (
	"net"
	"net/netip"
)

// Socket is a thin wrapper over a UDP socket that never blocks the game loop.
//
// C++/POSIX used fcntl(O_NONBLOCK) so a read returns immediately. Go has no
// O_NONBLOCK, and setting a past read deadline is a trap (it reports a timeout
// even when data is buffered). The idiomatic Go answer is a background goroutine
// that blocks on Read and hands packets to a buffered channel; the loop then does
// a non-blocking channel receive.
type Socket struct {
	conn *net.UDPConn
	in   chan inbound
	done chan struct{}
}

type inbound struct {
	from netip.AddrPort
	data []byte
}

// OpenSocket binds a UDP socket to a port (0 lets the OS choose, as clients do)
// and starts the background reader.
func OpenSocket(port int) (*Socket, error) {
	conn, err := net.ListenUDP("udp4", &net.UDPAddr{Port: port})
	if err != nil {
		return nil, err
	}
	s := &Socket{
		conn: conn,
		in:   make(chan inbound, 256),
		done: make(chan struct{}),
	}
	go s.readLoop()
	return s, nil
}

func (s *Socket) readLoop() {
	buf := make([]byte, 2048)
	for {
		n, from, err := s.conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			return // conn closed (Close was called) or fatal read error
		}
		data := append([]byte(nil), buf[:n]...) // copy out of the shared buffer
		select {
		case s.in <- inbound{from, data}:
		case <-s.done:
			return
		}
	}
}

// BoundPort reports the actual port we ended up bound to.
func (s *Socket) BoundPort() int {
	return s.conn.LocalAddr().(*net.UDPAddr).Port
}

// Send fires one datagram at a destination.
func (s *Socket) Send(dst netip.AddrPort, data []byte) error {
	_, err := s.conn.WriteToUDPAddrPort(data, dst)
	return err
}

// Receive returns the next datagram if one is waiting, or ok == false if not.
// It never blocks.
func (s *Socket) Receive() (data []byte, from netip.AddrPort, ok bool) {
	select {
	case p := <-s.in:
		return p.data, p.from, true
	default:
		return nil, netip.AddrPort{}, false
	}
}

// Close stops the reader and releases the socket.
func (s *Socket) Close() error {
	close(s.done)
	return s.conn.Close()
}
