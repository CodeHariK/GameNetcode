package netcode

import (
	"bytes"
	"net/netip"
	"testing"
	"time"
)

func localAddr(port int) netip.AddrPort {
	return netip.AddrPortFrom(netip.AddrFrom4([4]byte{127, 0, 0, 1}), uint16(port))
}

// waitForPacket polls a socket for up to timeout, returning the first datagram.
func waitForPacket(s *Socket, timeout time.Duration) ([]byte, bool) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if data, _, ok := s.Receive(); ok {
			return data, true
		}
		time.Sleep(time.Millisecond)
	}
	return nil, false
}

// A datagram sent on one socket is received byte-for-byte on another, and an
// empty socket reports "nothing waiting" instead of blocking.
func TestLoopback(t *testing.T) {
	server, err := OpenSocket(55581)
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	client, err := OpenSocket(0)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()

	if _, _, ok := server.Receive(); ok {
		t.Fatal("empty socket should report nothing waiting")
	}

	msg := []byte("hello netcode")
	if err := client.Send(localAddr(55581), msg); err != nil {
		t.Fatal(err)
	}
	got, ok := waitForPacket(server, 500*time.Millisecond)
	if !ok || !bytes.Equal(got, msg) {
		t.Fatalf("did not receive the message back (ok=%v)", ok)
	}
}

// With latency configured, a packet must not arrive before its delivery time,
// and must arrive once the simulator is updated past it.
func TestLatencyDelaysDelivery(t *testing.T) {
	server, _ := OpenSocket(55582)
	defer server.Close()
	client, _ := OpenSocket(0)
	defer client.Close()

	sim := NewNetworkSimulator(client, SimConfig{Latency: 80 * time.Millisecond})
	start := time.Now()
	sim.SendPacket(localAddr(55582), []byte("PING"), start)

	// Not due yet at +20ms: nothing should be on the wire.
	sim.Update(start.Add(20 * time.Millisecond))
	if _, ok := waitForPacket(server, 40*time.Millisecond); ok {
		t.Fatal("packet arrived before its latency window elapsed")
	}

	// Past the window: it flushes and arrives.
	sim.Update(start.Add(100 * time.Millisecond))
	if _, ok := waitForPacket(server, 500*time.Millisecond); !ok {
		t.Fatal("packet never arrived after latency elapsed")
	}
}

// Over many packets, the observed drop rate should be near the configured rate.
func TestStatisticalLoss(t *testing.T) {
	server, _ := OpenSocket(55583)
	defer server.Close()
	client, _ := OpenSocket(0)
	defer client.Close()

	const total = 500
	sim := NewNetworkSimulator(client, SimConfig{LossRate: 0.20})
	now := time.Now()
	for i := 0; i < total; i++ {
		sim.SendPacket(localAddr(55583), []byte("x"), now)
	}
	sim.Update(now) // no latency -> flush all survivors immediately

	st := sim.Stats()
	if st.Transmitted+st.Dropped != total {
		t.Fatalf("accounting off: %+v", st)
	}
	drop := float64(st.Dropped) / float64(st.Attempted)
	if drop < 0.13 || drop > 0.27 {
		t.Fatalf("drop rate %.2f not near 0.20", drop)
	}
	t.Logf("attempted=%d dropped=%d transmitted=%d (drop rate %.0f%%)",
		st.Attempted, st.Dropped, st.Transmitted, drop*100)
}
