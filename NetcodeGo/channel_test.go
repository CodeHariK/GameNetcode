package netcode

import (
	"encoding/binary"
	"net/netip"
	"testing"
	"time"
)

// feed walks a packed buffer of [MessageHeader|payload] slices and hands each message
// destined for ch to its ProcessIncoming — i.e. it plays the receiver's unpack step.
func feed(ch Channel, buf []byte) {
	off := 0
	for off+MessageHeaderSize <= len(buf) {
		chID := buf[off]
		msgID := binary.BigEndian.Uint16(buf[off+1 : off+3])
		msgLen := int(binary.BigEndian.Uint16(buf[off+3 : off+5]))
		if off+MessageHeaderSize+msgLen > len(buf) {
			break
		}
		if chID == ch.ChannelID() {
			ch.ProcessIncoming(msgID, buf[off+MessageHeaderSize:off+MessageHeaderSize+msgLen])
		}
		off += MessageHeaderSize + msgLen
	}
}

func drain(ch Channel) []string {
	var out []string
	for {
		m, ok := ch.ReceiveMessage()
		if !ok {
			return out
		}
		out = append(out, string(m.Payload))
	}
}

// Baseline: an unreliable-unordered channel delivers everything it's given.
func TestUnreliableUnorderedDelivers(t *testing.T) {
	now := time.Now()
	tx := NewUnreliableUnorderedChannel(0)
	tx.SendMessage([]byte("a"), now)
	tx.SendMessage([]byte("b"), now)
	tx.SendMessage([]byte("c"), now)

	buf := make([]byte, maxPacketSize)
	n := tx.WriteOutgoing(buf, now)
	if tx.HasOutgoing() {
		t.Fatal("all messages should have been packed")
	}

	rx := NewUnreliableUnorderedChannel(0)
	feed(rx, buf[:n])
	got := drain(rx)
	if len(got) != 3 || got[0] != "a" || got[1] != "b" || got[2] != "c" {
		t.Fatalf("expected [a b c], got %v", got)
	}
}

// A sequenced channel must drop a message older than the newest it has seen — state
// must never regress to a stale value.
func TestUnreliableSequencedDropsOlder(t *testing.T) {
	rx := NewUnreliableSequencedChannel(1)
	if !rx.ProcessIncoming(5, []byte("newer")) {
		t.Fatal("first message should be accepted")
	}
	if rx.ProcessIncoming(3, []byte("older")) {
		t.Fatal("an older sequence must be dropped")
	}
	if !rx.ProcessIncoming(6, []byte("newest")) {
		t.Fatal("a newer sequence should be accepted")
	}
	if rx.Dropped() != 1 {
		t.Fatalf("expected 1 dropped, got %d", rx.Dropped())
	}
	got := drain(rx)
	if len(got) != 2 || got[0] != "newer" || got[1] != "newest" {
		t.Fatalf("expected [newer newest], got %v", got)
	}
}

// Reliable-ordered must deliver in order even when messages arrive scrambled.
func TestReliableOrderedReassembly(t *testing.T) {
	rx := NewReliableOrderedChannel(2)
	rx.ProcessIncoming(0, []byte("m0"))
	rx.ProcessIncoming(2, []byte("m2")) // arrives before m1
	// With a gap (m1 missing), nothing past m0 may be delivered yet.
	if m, ok := rx.ReceiveMessage(); !ok || string(m.Payload) != "m0" {
		t.Fatalf("expected m0 first, got %q ok=%v", m.Payload, ok)
	}
	if _, ok := rx.ReceiveMessage(); ok {
		t.Fatal("m2 must not be released while m1 is missing (head-of-line)")
	}
	rx.ProcessIncoming(1, []byte("m1")) // gap filled
	if m, ok := rx.ReceiveMessage(); !ok || string(m.Payload) != "m1" {
		t.Fatalf("expected m1, got %q ok=%v", m.Payload, ok)
	}
	if m, ok := rx.ReceiveMessage(); !ok || string(m.Payload) != "m2" {
		t.Fatalf("expected m2, got %q ok=%v", m.Payload, ok)
	}
}

// Reliable-ordered must retransmit an unacked message on its RTT timer, then stop once
// the carrying packet is acked.
func TestReliableOrderedAckAndRetransmit(t *testing.T) {
	t0 := time.Now()
	tx := NewReliableOrderedChannel(2)
	tx.SetCurrentPacketSequence(100)
	tx.SendMessage([]byte("hi"), t0)

	buf := make([]byte, maxPacketSize)
	if n := tx.WriteOutgoing(buf, t0); n == 0 {
		t.Fatal("first send should write the message")
	}
	// Immediately after, it's not yet due for retransmission.
	if n := tx.WriteOutgoing(buf, t0); n != 0 {
		t.Fatal("should not retransmit before the RTO elapses")
	}
	// After the RTO (~65ms for default RTT), it retransmits.
	if n := tx.WriteOutgoing(buf, t0.Add(200*time.Millisecond)); n == 0 {
		t.Fatal("should retransmit once the RTO has elapsed")
	}
	if tx.PendingUnacked() != 1 {
		t.Fatalf("message should still be unacked, pending=%d", tx.PendingUnacked())
	}
	// Ack the packet that carried it -> stop tracking / retransmitting.
	tx.OnPacketAcked(100)
	if tx.PendingUnacked() != 0 || tx.HasOutgoing() {
		t.Fatalf("ack should clear the message, pending=%d", tx.PendingUnacked())
	}
	if n := tx.WriteOutgoing(buf, t0.Add(1*time.Second)); n != 0 {
		t.Fatal("nothing should be sent after the message is acked")
	}
}

// End-to-end over loopback: a reliable message survives the full connection path
// (flush -> UDP -> unpack -> reassembly -> ack).
func TestConnectionReliableMessageEndToEnd(t *testing.T) {
	serverSock, err := OpenSocket(0)
	if err != nil {
		t.Fatalf("server socket: %v", err)
	}
	defer serverSock.Close()
	clientSock, err := OpenSocket(0)
	if err != nil {
		t.Fatalf("client socket: %v", err)
	}
	defer clientSock.Close()

	lo := netip.AddrFrom4([4]byte{127, 0, 0, 1})
	serverAddr := netip.AddrPortFrom(lo, uint16(serverSock.BoundPort()))

	now := time.Now()
	client := NewConnection(clientSock, 5*time.Second, 250*time.Millisecond)
	client.RegisterChannel(NewReliableOrderedChannel(2))
	client.Connect(serverAddr, now)

	server := NewConnection(serverSock, 5*time.Second, 250*time.Millisecond)
	server.RegisterChannel(NewReliableOrderedChannel(2))

	client.SendMessage(2, []byte("hello reliable"), now)

	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		now = time.Now()
		client.Update(now)

		if data, from, ok := serverSock.Receive(); ok {
			if server.State() == Disconnected {
				server.Accept(from, now)
			}
			server.ProcessPacket(from, data, now)
		}
		server.Update(now)

		if data, from, ok := clientSock.Receive(); ok {
			client.ProcessPacket(from, data, now)
		}

		if m, ok := server.ReceiveMessage(); ok {
			if string(m.Payload) != "hello reliable" {
				t.Fatalf("got %q, want %q", m.Payload, "hello reliable")
			}
			return // delivered
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("reliable message was never delivered end-to-end")
}
