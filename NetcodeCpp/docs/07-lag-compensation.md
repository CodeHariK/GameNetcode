# Milestone 7 — Lag compensation (server-side hit rewind)

**What this milestone adds:** fair shooting. If you clearly hit a moving target on
your screen, it should count — even though, by the time your shot reaches the
server, the target has moved.

New files: `src/game/LagCompensation`, `tests/lagcomp_test.cpp`. `GameTypes` gains
`FireCommand`, `HitNotification`, and a `ReliableMsgType` tag. The server gains a
target bot and a world-history buffer.

## The problem

Because of interpolation (m04) and network latency, a client sees other entities
**in the past**. If the server hit-tested against the *present* world, players would
have to "lead" every shot and obvious hits would miss. This is the single most
common source of "I definitely hit them!" complaints.

## The solution: rewind

The server records where **every** entity was on **every tick** (a short history).
When a shot arrives, the server rewinds the world to the exact moment the shooter
was looking at, runs the hit test *there*, then continues. This is Valve's Source
"what you see is what you get" model.

The client stamps each shot with `view_server_tick` — the snapshot it was actually
looking at — so the server rewinds to precisely that tick rather than guessing.

## The pieces

### `LagCompensation`
- `WorldHistory` — a ring buffer of time-stamped world states (one per tick, ~2 s
  deep). Look up an exact tick with `get_at_tick`, or interpolate by wall-clock time
  with `sample` (used as a fallback from RTT when the exact tick isn't stored).
- `ray_circle_intersect(origin, dir, center, radius, out_t)` — the geometry: does a
  ray hit a circular hitbox, and at what distance.
- `hitscan(origin, aim, entities, shooter_id, radius)` — fire a ray through the
  (rewound) world and return the nearest entity struck, skipping the shooter.

### `GameTypes` additions (reliable-channel messages)
- `ReliableMsgType { MSG_FIRE, MSG_HIT }` — a 1-byte tag so the reliable channel can
  carry more than one message kind.
- `FireCommand` — `client_tick`, `view_server_tick`, `origin`, `aim`.
- `HitNotification` — `hit`, `target_id`, hit point.

### Apps
The server spawns a fast sine-sweeping **bot**, records the full world each tick, and
on a `FireCommand` rewinds and hit-tests, replying with a `HitNotification`. The
client auto-fires at where it *sees* the bot and tallies hits.

## What `lagcomp_test` verifies, and why

- `ray_circle_intersect` hit distance, plus parallel and backward misses.
- `hitscan` returns the *nearest* target and skips the shooter.
- `WorldHistory` interpolation, clamping, and exact-tick lookup.
- **The key test:** a shot that *misses* against the present world *hits* after
  rewinding to what the shooter saw. *This is lag compensation, proven in isolation
  and deterministically — the live demo's tiny localhost latency can't show it as
  clearly.*

## Run it

```
make test        # adds lagcomp_test
make run         # client hits the moving bot ~100%
```

## Mental model

The referee keeps a DVR of the whole match. When you say "I shot at 12.300s," the
referee scrubs the DVR back to 12.300s, checks whether your crosshair was on the
target *then*, and rules accordingly — not on where everyone is now.
