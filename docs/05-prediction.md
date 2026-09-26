# 05 — Client prediction & reconciliation

**Concept.** Make the local player feel instant even though the server is authority
and is milliseconds away.

## The problem
Waiting for the server to confirm each move makes controls feel laggy.

## The idea
1. **Predict**: apply your input immediately using the *same* simulation the server
   runs, so movement is instant.
2. **Server stays authoritative**: it re-runs your inputs and, in each snapshot,
   reports the last input tick it processed.
3. **Reconcile**: when a snapshot arrives, compare the server's state at that acked
   tick to what you predicted. Match → carry on. Mismatch → snap to the server state
   and **replay** the inputs newer than that tick.

This only works because the simulation is **deterministic** (same start + same
inputs → same result) on both sides. Recent inputs are sent **redundantly** so a
lost input packet usually doesn't cost the server an input. (Reconciliation is a
limited form of the *rollback* used by fighting games.)

## What we test (and why)
- Determinism; no false divergence when both ran the same inputs; divergence is
  detected and replay converges to the server's ground truth; redundant batching.

## Code & deep dive
- C++: `NetcodeCpp/src/game/{Simulation,InputHistory}`
  · deep guide `NetcodeCpp/docs/05-prediction.md`
- Go: not yet ported.
