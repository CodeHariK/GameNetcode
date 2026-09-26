# 04 — Authoritative server & snapshot interpolation

**Concept.** One server owns the world and broadcasts it; clients show it smoothly
even though updates arrive only ~20 times a second.

## The problem
Clients can't be trusted, and sending 60 full world states per second is wasteful.

## The idea
The **server is authoritative**: it simulates at 60 Hz but broadcasts snapshots at
20 Hz. A client that drew each snapshot on arrival would stutter, so it renders the
world ~100 ms in the past and **interpolates** between the two snapshots that
straddle that time — always drawing between two known states rather than guessing.
A small delay buys smoothness.

Key distinction: **tick rate (simulate finely) vs send rate (send coarsely)**, with
interpolation hiding the low send rate.

## What we test (and why)
No standalone test here; the deterministic physics this leads into is covered in 05,
and interpolation is visible by running the demo.

## Code & deep dive
- C++: `NetcodeCpp/src/game/{GameTypes,SnapshotBuffer}`, `src/apps/{server,client}`
  · deep guide `NetcodeCpp/docs/04-snapshots.md` · run: `cd NetcodeCpp && make run`
- Go: not yet ported.
