package netcode

import (
	"math/rand"
	"net/netip"
	"sort"
	"time"
)

// SimConfig describes the fake network conditions to inject.
type SimConfig struct {
	Latency  time.Duration // one-way added delay
	Jitter   time.Duration // +/- random variance on the delay
	LossRate float64       // [0,1] chance to drop a packet
	DupRate  float64       // [0,1] chance to also send a duplicate
}

// SimStats is simple accounting for tests / telemetry.
type SimStats struct {
	Attempted   int
	Dropped     int
	Transmitted int
	Duplicated  int
}

type pending struct {
	dst     netip.AddrPort
	data    []byte
	deliver time.Time
}

// NetworkSimulator wraps a Socket and injects latency, jitter, loss and
// duplication on the OUTGOING path. On localhost the network is perfect, so this
// is how later milestones (prediction, lag comp, ...) get tested under stress.
//
// You call SendPacket(dst, data, now) to schedule a packet, and Update(now) each
// frame to flush the ones whose delivery time has arrived to the real socket.
type NetworkSimulator struct {
	socket *Socket
	cfg    SimConfig
	stats  SimStats
	queue  []pending
	rng    *rand.Rand
}

func NewNetworkSimulator(socket *Socket, cfg SimConfig) *NetworkSimulator {
	return &NetworkSimulator{
		socket: socket,
		cfg:    cfg,
		rng:    rand.New(rand.NewSource(time.Now().UnixNano())),
	}
}

// SendPacket schedules a packet for (maybe) later delivery. It may be dropped,
// delayed, or duplicated according to the config.
func (ns *NetworkSimulator) SendPacket(dst netip.AddrPort, data []byte, now time.Time) {
	ns.stats.Attempted++

	if ns.cfg.LossRate > 0 && ns.rng.Float64() < ns.cfg.LossRate {
		ns.stats.Dropped++
		return
	}

	jitter := time.Duration(0)
	if ns.cfg.Jitter > 0 {
		jitter = time.Duration((ns.rng.Float64()*2 - 1) * float64(ns.cfg.Jitter))
	}
	delay := ns.cfg.Latency + jitter
	if delay < 0 {
		delay = 0
	}
	deliver := now.Add(delay)

	// Copy the payload: the caller may reuse its buffer after this returns.
	payload := append([]byte(nil), data...)

	if ns.cfg.DupRate > 0 && ns.rng.Float64() < ns.cfg.DupRate {
		ns.stats.Duplicated++
		dup := append([]byte(nil), payload...)
		ns.queue = append(ns.queue, pending{dst, dup, deliver.Add(5 * time.Millisecond)})
	}
	ns.queue = append(ns.queue, pending{dst, payload, deliver})
}

// Update flushes every queued packet whose delivery time has arrived, in time
// order. (A heap would be faster; a sort keeps this milestone readable.)
func (ns *NetworkSimulator) Update(now time.Time) {
	if len(ns.queue) == 0 {
		return
	}
	sort.Slice(ns.queue, func(i, j int) bool {
		return ns.queue[i].deliver.Before(ns.queue[j].deliver)
	})
	i := 0
	for i < len(ns.queue) && !ns.queue[i].deliver.After(now) {
		_ = ns.socket.Send(ns.queue[i].dst, ns.queue[i].data)
		ns.stats.Transmitted++
		i++
	}
	ns.queue = ns.queue[i:]
}

// Pending is how many packets are still in flight in the delay buffer.
func (ns *NetworkSimulator) Pending() int { return len(ns.queue) }

// Stats returns the running counters.
func (ns *NetworkSimulator) Stats() SimStats { return ns.stats }
