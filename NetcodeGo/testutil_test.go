package netcode

import (
	"net/netip"
	"time"
)

// waitInbound polls a socket for up to timeout, returning the first datagram and
// who sent it.
func waitInbound(s *Socket, timeout time.Duration) ([]byte, netip.AddrPort, bool) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if data, from, ok := s.Receive(); ok {
			return data, from, true
		}
		time.Sleep(time.Millisecond)
	}
	return nil, netip.AddrPort{}, false
}
