# Milestone 8 — Interest management (area of interest)

**What this milestone adds:** stop telling each client about entities it can't see,
so bandwidth scales with what a player *perceives*, not with the size of the whole
world. This is how large sessions stay within budget.

New files: `src/game/InterestManagement`, `tests/aoi_test.cpp`. The server now
roams several bots and keeps a **per-client** delta baseline.

## The problem

In a big world, sending every client every entity is wasteful and doesn't scale. A
client only cares about what's near it.

## The pieces

### `InterestManagement`
`compute_visible_set(viewer_id, entities, radius, max_count)` — returns the viewer
plus every entity within an interest `radius`, optionally capped to the nearest
`max_count` (a hard per-snapshot ceiling so a crowd can't blow the budget). The
viewer is always included so its own prediction/reconciliation keeps working.

### The consequence: per-client baselines
Delta compression (m06) encodes against a baseline the client holds. But now **each
client receives a different subset**, so the baseline must be per client. The server
stores, per client, the exact visible subset it sent at each tick and deltas the next
snapshot against *that*. Entities entering range appear as "new"; entities leaving
range simply stop being sent (the decoder drops anything not listed).

Crucially, the **full-world history for lag comp (m07) stays global** — culling only
affects what each client is *sent*, never hit detection, so shots remain fair for
anything the client could see.

### Apps
The server spawns 6 bots in different lanes/phases and, per client, sends only the
visible subset (delta-encoded against that client's baseline). The client targets
the **nearest visible** entity (re-evaluated each snapshot, since targets pop in and
out of range) and reports how many entities it currently sees.

## What `aoi_test` verifies, and why

- Only in-range entities (plus the viewer) are visible; far ones are culled.
- The viewer is always included, even when alone.
- No viewer in the set → empty result (can't see from a viewpoint that isn't there).
- `max_count` keeps the viewer plus the nearest N−1.
- The radius bound is inclusive.

*These pin down the exact culling rules the per-client baseline logic depends on.*

## Run it

```
make test        # adds aoi_test
make run         # server has 7 entities; client "Visible" rises/falls (usually 1-5)
```

## Mental model

You don't get a live feed of the entire city — only of your own neighbourhood. As
you move, buildings enter and leave your feed. The city still exists in full at the
station (for fair replays); you're just not paying to stream all of it.
