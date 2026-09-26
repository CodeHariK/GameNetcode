package netcode

import (
	"bytes"
	"testing"
	"time"
)

// 16-bit sequence numbers wrap; "newer" must be the short way round the circle.
func TestSequenceGreaterThan(t *testing.T) {
	if !sequenceGreaterThan(1, 0) {
		t.Fatal("1 should be newer than 0")
	}
	if !sequenceGreaterThan(0, 65535) {
		t.Fatal("0 should be newer than 65535 (across the wrap)")
	}
	if sequenceGreaterThan(65535, 0) {
		t.Fatal("65535 should be older than 0 (across the wrap)")
	}
}

// A header survives a round trip, and a foreign / short packet is rejected.
func TestHeaderRoundTrip(t *testing.T) {
	h := PacketHeader{ProtocolID: ProtocolID, Sequence: 1000, Ack: 999, AckBits: 0xABCD1234}
	got, ok := ParseHeader(h.Serialize())
	if !ok || got != h {
		t.Fatalf("round trip failed: %+v vs %+v (ok=%v)", got, h, ok)
	}
	if len(h.Serialize()) != HeaderSize {
		t.Fatalf("header should be %d bytes", HeaderSize)
	}
	bad := h.Serialize()
	bad[0] ^= 0xFF // corrupt the protocol id
	if _, ok := ParseHeader(bad); ok {
		t.Fatal("wrong protocol id should be rejected")
	}
	if _, ok := ParseHeader([]byte{1, 2, 3}); ok {
		t.Fatal("short buffer should be rejected")
	}
}

// ackBits encodes exactly which of the last 32 sequences were received.
func TestAckBits(t *testing.T) {
	r := NewReliabilitySystem()
	now := time.Now()
	for _, seq := range []uint16{0, 1, 3} { // received 0,1,3 but not 2
		r.PacketReceived(PacketHeader{ProtocolID: ProtocolID, Sequence: seq}, now)
	}
	// remoteSequence is 3 (carried in Ack). ackBits covers 2,1,0 -> bits for 1 and 0 set.
	if got := r.ackBits(); got != (1<<1)|(1<<2) {
		t.Fatalf("ackBits = %b, want %b", got, (1<<1)|(1<<2))
	}
}

// A two-way exchange measures round-trip time from the ack of our sent packet.
func TestRTTFromAcks(t *testing.T) {
	client := NewReliabilitySystem()
	server := NewReliabilitySystem()
	t0 := time.Now()

	hClient := client.GenerateHeader(t0)                        // client sends seq 0 at t0
	server.PacketReceived(hClient, t0)                          // server gets it
	hServer := server.GenerateHeader(t0)                        // server replies, acking client seq 0
	client.PacketReceived(hServer, t0.Add(40*time.Millisecond)) // arrives 40ms later

	if rtt := client.RTTms(); rtt < 39 || rtt > 41 {
		t.Fatalf("measured RTT %.1fms, want ~40ms", rtt)
	}
}

// Full handshake over loopback: client connects, server accepts, both exchange
// payloads and the client reaches Connected with a measured RTT.
func TestConnectionHandshake(t *testing.T) {
	serverSock, err := OpenSocket(55591)
	if err != nil {
		t.Fatal(err)
	}
	defer serverSock.Close()
	clientSock, err := OpenSocket(0)
	if err != nil {
		t.Fatal(err)
	}
	defer clientSock.Close()

	t0 := time.Now()
	client := NewConnection(clientSock, 5*time.Second, 250*time.Millisecond)
	client.Connect(localAddr(55591), t0)
	if err := client.SendPacket([]byte("hello"), t0); err != nil {
		t.Fatal(err)
	}

	data, from, ok := waitInbound(serverSock, 500*time.Millisecond)
	if !ok {
		t.Fatal("server received nothing")
	}
	server := NewConnection(serverSock, 5*time.Second, 250*time.Millisecond)
	server.Accept(from, t0)
	payload, ok := server.ProcessPacket(from, data, t0)
	if !ok || !bytes.Equal(payload, []byte("hello")) {
		t.Fatalf("server payload wrong: %q ok=%v", payload, ok)
	}

	if err := server.SendPacket([]byte("welcome"), t0.Add(40*time.Millisecond)); err != nil {
		t.Fatal(err)
	}
	data2, from2, ok := waitInbound(clientSock, 500*time.Millisecond)
	if !ok {
		t.Fatal("client received nothing")
	}
	payload2, ok := client.ProcessPacket(from2, data2, t0.Add(40*time.Millisecond))
	if !ok || !bytes.Equal(payload2, []byte("welcome")) {
		t.Fatalf("client payload wrong: %q ok=%v", payload2, ok)
	}
	if client.State() != Connected {
		t.Fatalf("client should be Connected, got %v", client.State())
	}
	if rtt := client.RTTms(); rtt < 39 || rtt > 41 {
		t.Fatalf("client RTT %.1fms, want ~40ms", rtt)
	}
}
