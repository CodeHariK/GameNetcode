# Milestone 4 — Authoritative server & snapshot interpolation

**What this milestone adds:** the first runnable server and client. The server owns
the world and broadcasts it; the client renders it *smoothly* even though updates
arrive only 20 times a second.

New files: `src/game/GameTypes`, `src/game/SnapshotBuffer`,
`src/apps/server.cpp`, `src/apps/client.cpp`.

## Why a server owns the world

In a competitive game clients can't be trusted (they can lie about positions). One
**authoritative server** runs the real simulation; clients display what it says.
The server simulates at **60 Hz** but only **broadcasts at 20 Hz** to save
bandwidth — sending 60 full world states per second would be wasteful.

## The pieces

### `GameTypes` — the shared vocabulary
- `Vec2` — a 2D vector with `+ - *` and `lerp` (interpolation helper).
- `EntityState` — one object in the world: `entity_id`, `position`, `velocity`,
  `color`. It's a **packed** struct (`#pragma pack`) so its byte layout is fixed
  and it can be memcpy'd straight onto the wire.
- `SnapshotHeader` — precedes an array of `EntityState` in a snapshot
  (`server_tick`, `entity_count`).
- `Channels` — the channel-id constants from m03.

### `SnapshotBuffer` — smoothing out 20 Hz updates
If the client drew each snapshot the instant it arrived, motion would stutter (20
updates/sec, arriving with jitter). Instead the client keeps recent snapshots and
renders the world at `now − interpolation_delay` (≈100 ms in the past),
**interpolating** between the two snapshots that straddle that time. You always draw
*between two known states* instead of guessing, at the cost of a little delay.
Key API: `add_snapshot(header, states, now)` and `get_interpolated_state(now, out)`.

### `server.cpp` (m04)
A 60 Hz loop that: accepts clients (each gets a `Connection` with the three
channels), moves one scripted entity along a path, and every 3rd tick (20 Hz)
memcpy's `SnapshotHeader + EntityState[]` onto `CH_STATE` for each client. No client
input yet — that's m05.

### `client.cpp` (m04)
Connects (its heartbeats make the server notice it), receives raw snapshots into a
`SnapshotBuffer`, and every second prints the *interpolated* position of the moving
entity. It's a pure observer at this stage.

## Key idea: tick rate vs send rate

Simulating at 60 Hz but sending at 20 Hz is deliberate — simulate finely for
correctness, send coarsely for bandwidth, and hide the low send rate on the client
with interpolation. This "delay a little to interpolate" trade is everywhere in
games.

## Run it

```
make run         # server + client; watch the client interpolate the mover
```

There's no standalone m04 test; the determinism this later relies on is covered by
`prediction_test` in m05, and you can watch interpolation live with `make run`.

## Mental model

The server is the single source of truth, filming the world at 60 fps but only
mailing you 20 photos a second. The client plays those photos slightly delayed and
tweens between them, so motion looks smooth.
