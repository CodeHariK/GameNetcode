package netcode

import "time"

// ReliabilitySystem turns the packet header's sequence/ack/ackBits into useful
// facts: which of our packets the peer received, the round-trip time, and an
// estimate of packet loss. It does not retransmit anything itself — it reports
// acks (via a callback) so higher layers (channels, later) can decide what to do.
type ReliabilitySystem struct {
	localSequence  uint16 // next sequence we will send
	remoteSequence uint16 // highest sequence we've received
	hasRemote      bool

	received map[uint16]bool     // sequences we've received (for ackBits)
	sent     map[uint16]sentInfo // our sent packets awaiting ack

	rttMs    float64
	lossRate float64

	ackCallback func(seq uint16)
}

type sentInfo struct {
	time  time.Time
	acked bool
}

const relWindow = 256 // how many past sequences we track

func NewReliabilitySystem() *ReliabilitySystem {
	return &ReliabilitySystem{
		received: make(map[uint16]bool),
		sent:     make(map[uint16]sentInfo),
	}
}

func (r *ReliabilitySystem) SetAckCallback(cb func(seq uint16)) { r.ackCallback = cb }
func (r *ReliabilitySystem) RTTms() float64                     { return r.rttMs }
func (r *ReliabilitySystem) PacketLoss() float64                { return r.lossRate }

func (r *ReliabilitySystem) Reset() {
	r.localSequence, r.remoteSequence, r.hasRemote = 0, 0, false
	r.received = make(map[uint16]bool)
	r.sent = make(map[uint16]sentInfo)
	r.rttMs, r.lossRate = 0, 0
}

// GenerateHeader builds the header for a packet we're about to send, recording it
// so we can measure RTT when it's acked.
func (r *ReliabilitySystem) GenerateHeader(now time.Time) PacketHeader {
	h := PacketHeader{
		ProtocolID: ProtocolID,
		Sequence:   r.localSequence,
		Ack:        r.remoteSequence,
		AckBits:    r.ackBits(),
	}
	r.sent[r.localSequence] = sentInfo{time: now}
	r.localSequence++
	r.prune()
	return h
}

// ackBits sets bit n for each of the last 32 sequences we've received.
func (r *ReliabilitySystem) ackBits() uint32 {
	var bits uint32
	for i := uint16(1); i <= 32; i++ {
		if r.received[r.remoteSequence-i] {
			bits |= 1 << (i - 1)
		}
	}
	return bits
}

// PacketReceived records an incoming packet: update the remote sequence, remember
// it for our own ackBits, and process the acks it carries for our sent packets.
func (r *ReliabilitySystem) PacketReceived(h PacketHeader, now time.Time) {
	r.received[h.Sequence] = true
	if !r.hasRemote || sequenceGreaterThan(h.Sequence, r.remoteSequence) {
		r.remoteSequence = h.Sequence
		r.hasRemote = true
	}
	r.ackPacket(h.Ack, now)
	for i := uint16(0); i < 32; i++ {
		if h.AckBits&(1<<i) != 0 {
			r.ackPacket(h.Ack-(i+1), now)
		}
	}
}

func (r *ReliabilitySystem) ackPacket(seq uint16, now time.Time) {
	s, ok := r.sent[seq]
	if !ok || s.acked {
		return
	}
	s.acked = true
	r.sent[seq] = s

	sample := float64(now.Sub(s.time).Microseconds()) / 1000.0 // ms
	if r.rttMs == 0 {
		r.rttMs = sample
	} else {
		r.rttMs += (sample - r.rttMs) * 0.1 // exponential moving average
	}
	if r.ackCallback != nil {
		r.ackCallback(seq)
	}
}

// Update recomputes the loss estimate from packets that have aged out unacked.
func (r *ReliabilitySystem) Update(now time.Time) {
	var total, lost int
	for seq, s := range r.sent {
		if r.localSequence-seq > relWindow {
			continue // too old to still be in play
		}
		if now.Sub(s.time) > time.Second {
			total++
			if !s.acked {
				lost++
			}
		}
	}
	if total > 0 {
		r.lossRate = float64(lost) / float64(total)
	}
}

// prune keeps the tracking maps bounded to the recent window.
func (r *ReliabilitySystem) prune() {
	for seq := range r.sent {
		if r.localSequence-seq > relWindow {
			delete(r.sent, seq)
		}
	}
	for seq := range r.received {
		if r.remoteSequence-seq > relWindow {
			delete(r.received, seq)
		}
	}
}
