# Milestone 5 — Client prediction & server reconciliation

**What this milestone adds:** input. If the client waited for the server to confirm
every move, controls would feel laggy. So the client acts *immediately* and quietly
corrects itself when the server disagrees.

New files: `src/game/Simulation`, `src/game/InputHistory`,
`tests/prediction_test.cpp`. `GameTypes` gains `PlayerInput`, `InputBatchHeader`,
and a `last_client_input_tick` field on `SnapshotHeader`.

## The three ideas

1. **Prediction** — the client applies your input right now with the *same*
   simulation the server runs, so movement is instant.
2. **The server is still authoritative** — it re-runs your inputs and, in every
   snapshot, reports the last input tick it has processed.
3. **Reconciliation** — when a snapshot arrives, the client compares the server's
   state at that acked tick to what it predicted. Match → carry on. Mismatch → snap
   to the server's state and **replay** the inputs newer than that tick. The player
   barely notices; the world self-corrects.

This only works because the simulation is **deterministic**: same start + same
inputs = same result, on both machines.

## The pieces

### `Simulation` — the shared, deterministic physics
`simulate_player(EntityState&, PlayerInput, dt)` — acceleration, friction, max
speed, integrate position, clamp to the world. Deliberately simple and *identical*
on client and server. This determinism is the linchpin of the whole milestone.

### `GameTypes` additions
- `PlayerInput` — one tick's input (`tick`, `move_x`, `move_y`, `buttons`).
- `InputBatchHeader` — precedes an array of inputs in a datagram.
- `SnapshotHeader.last_client_input_tick` — the server's "I've processed up to
  here" acknowledgement, the anchor for reconciliation.

### `InputHistory` — the client's memory
Stores recent `(input, predicted_state)` pairs. Provides:
- `record_input` — remember what you did and where it put you.
- `has_diverged(ack_tick, server_state)` — did the server end up somewhere different
  from our prediction at that tick?
- `reconcile(ack_tick, server_state, dt, out)` — snap to the server state and replay
  all still-unacknowledged inputs, returning how many were replayed.
- `discard_acknowledged` — drop inputs the server has confirmed.
- `get_recent_inputs(n)` — the last *n* inputs, sent **redundantly** every packet so
  a single dropped input packet usually doesn't cost the server an input.

### Apps
The client predicts locally each tick, sends a redundant input batch, and reconciles
on each snapshot. The server applies each client's inputs to its entity and stamps
snapshots with the last input tick processed.

## What `prediction_test` verifies, and why

1. **Determinism** — the same inputs from the same start produce identical states.
   *Without this, prediction and reconciliation are impossible.*
2. **No false divergence** — when client and server ran the same inputs, `has_diverged`
   is false. *We must not reconcile when nothing is wrong.*
3. **Divergence detected + replay converges** — an artificial server correction is
   detected, and replaying pending inputs lands exactly on the server's ground
   truth. *This is reconciliation working.*
4. **Redundant batching** — the last-N inputs are the right ones. *This is our
   cheap insurance against loss.*

## Run it

```
make test        # adds prediction_test
make run         # 0 reconciliations on a clean link (prediction was right)
```

## Mental model

You move the instant you press a key (prediction). The server is the referee; when
its ruling reaches you and differs, you rewind to the ruling and re-apply the keys
you've pressed since (reconciliation) — so fast you don't feel it.
