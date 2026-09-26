# Code Review — game-netcode

A learning-oriented review: what's solid, what's rough, and where to simplify as
you keep learning. Nothing here is a bug that breaks the demo — all 9 test suites
pass and the server/client run correctly. These are clarity and design notes.

## What's genuinely good

- **Clean layering.** `net/` (reusable engine) vs. `game/` (this game) vs.
  `apps/` (wiring) is the right split, and each header does one thing.
- **Deterministic simulation shared by client and server.** This is the
  foundation that makes prediction/reconciliation and lag comp possible, and it's
  kept in one small file (`game/Simulation.hpp`).
- **A test per concept.** `make test` is a fast, readable safety net, and the
  lag-comp and delta tests assert the *interesting* behavior, not just round-trips.
- **The wire format is honest about bytes** — `#pragma pack`, fixed-width headers,
  quantization — which is exactly the mindset networked games need.

## Things to simplify / watch (by priority)

### 1. `apps/server.cpp` does too much in one function  *(clarity)*
`main()` drains the socket, parses inputs, runs the fire/hitscan path, moves bots,
culls by AOI, delta-encodes, and prints telemetry — ~300 lines. This is the
single biggest "too much" in the project.
**Suggestion:** extract free functions like `handle_input_message(...)`,
`handle_fire_command(...)`, and `broadcast_snapshots(...)`. Same behavior, far
easier to read one concern at a time. (Good first refactor to try yourself.)

### 2. `game/SnapshotBuffer.hpp` is built but not wired in  *(dead-ish code)*
It implements remote-entity interpolation (step 4), but the current client
consumes decoded snapshots directly and doesn't render other players, so nothing
calls it. It's not wrong — it's the piece you'd use once you add rendering — but a
reader can't tell that from the code.
**Suggestion:** either use it for remote entities in the client, or add a one-line
comment at the top: "Reference implementation for when rendering interpolates
remote players; not yet used by the demo."

### 3. The client guesses which entity is "itself"  *(design gap)*
`client.cpp` assumes its own entity is the lowest id and the target is the nearest
other entity. That works only because the demo has one player. The server never
tells the client its `entity_id`.
**Suggestion (future):** on connect, have the server send the client its
`entity_id` once over the reliable channel. Then "self" and "others" are
unambiguous with real multiplayer.

### 4. Constants are scattered  *(minor clarity)*
Tick rate (60), snapshot rate (3), AOI radius (35), quantization ranges, bot
specs, hit radius — spread across `server.cpp`, `client.cpp`, and the game
headers.
**Suggestion:** a single `game/Config.hpp` of `constexpr` values makes the knobs
discoverable and keeps client/server in sync by construction.

### 5. Scripted bots have position but zero velocity  *(minor correctness)*
Server bots are moved by setting position directly and leaving velocity at 0 (to
avoid blowing the velocity quantization range). It's commented, but it means a bot
"teleports" from the physics' point of view. Fine for a target dummy; just know
it's not a real simulated body.

### 6. `InputHistory::find_input` is a linear scan  *(perf, not urgent)*
`has_diverged` scans the history each snapshot. At ~a few hundred entries this is
nothing, but if you raise history size or client count, a map keyed by tick would
be the move.

## Tiny cleanups already applied

- Removed the only compiler warning (`unused parameter 'current_time'` in
  `net/ReliableOrderedChannel.hpp`) by commenting the parameter name. The build is
  now warning-clean under `-Wall -Wextra`.

## Optional next steps that would aid learning

- Add a `.clang-format` (you like one-parameter-per-line) and format the tree, so
  style is automatic rather than manual.
- A `docs/`-linked diagram of the packet header and the delta-snapshot layout.
- A second client instance to see real player-vs-player interpolation and lag
  compensation (currently the "other" entities are scripted bots).
