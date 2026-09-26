# 08 — Interest management (area of interest)

**Concept.** Only tell each client about entities it can perceive, so bandwidth
scales with what a player sees, not with total world size.

## The idea
Per client, compute a **visible set** — the player plus entities within an interest
radius, optionally capped to the nearest N — and send only that. Entities entering
range appear as "new"; entities leaving range simply stop being sent.

Because each client now receives a different subset, the **delta baseline becomes
per-client**: the server stores the exact visible subset it sent each client and
deltas against that. The full-world history used for lag compensation stays global,
so hit tests remain fair for anything the client could see.

## What we test (and why)
- Only in-range entities (plus the viewer) are visible; viewer always included;
  nearest-N cap; inclusive radius bound — the exact rules the per-client baseline
  depends on.

## Code & deep dive
- C++: `NetcodeCpp/src/game/InterestManagement`
  · deep guide `NetcodeCpp/docs/08-interest-management.md`
- Go: not yet ported.
