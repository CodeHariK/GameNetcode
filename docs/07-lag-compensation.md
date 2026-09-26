# 07 — Lag compensation (server-side hit rewind)

**Concept.** Make shooting fair: if you clearly hit a moving target on your screen,
it should count, even though the target has moved by the time your shot reaches the
server.

## The problem
Because of interpolation and latency, a client sees other entities **in the past**.
Hit-testing against the *present* world would force players to lead every shot and
make obvious hits miss.

## The idea
The server records where **every** entity was on **every tick** (a short history).
When a shot arrives — stamped by the client with the tick it was viewing — the server
**rewinds** the world to that moment, runs the hit test there, then continues. This
is Valve's "what you see is what you get" model. A `FireCommand` (reliable channel)
is answered with an authoritative `HitNotification`.

## What we test (and why)
- Ray/hitbox geometry and nearest-target selection.
- World-history lookup (exact tick + interpolation).
- **The key test**: a shot that *misses* against the present world *hits* after
  rewinding to what the shooter saw — lag compensation, proven deterministically.

## Code & deep dive
- C++: `NetcodeCpp/src/game/LagCompensation`, `FireCommand`/`HitNotification` in `GameTypes`
  · deep guide `NetcodeCpp/docs/07-lag-compensation.md`
- Go: not yet ported.
