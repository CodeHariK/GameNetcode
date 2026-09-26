# 06 — Bit packing & delta compression

**Concept.** Bandwidth is the real budget. Make snapshots small: pack values in the
minimum bits, and send only what changed.

## The idea
- **Bit packing**: a bool is 1 bit; a value in `[0,1000]` is 10 bits; a position is
  *quantized* to a grid and packed in ~16 bits instead of a 32-bit float.
- **Delta compression**: send each entity as a delta against a **baseline** the
  client has acknowledged — one "changed?" bit per field, value only if changed. An
  unchanging colour collapses to 1 bit; a still entity costs almost nothing.

Baseline choice (Quake3-style): the client tells the server the newest snapshot tick
it decoded; the server deltas against exactly that, or sends an absolute keyframe if
that baseline has aged out.

Quantization is lossy but far finer than the reconciliation threshold, so it never
triggers a false correction.

## What we test (and why)
- Bit round-trips (incl. ranged ints, quantized floats, overflow safety); delta
  encode/decode round-trips, new/removed entities, and a measured ~85% size cut.

## Code & deep dive
- C++: `NetcodeCpp/src/net/BitStream`, `src/game/DeltaSnapshot`
  · deep guide `NetcodeCpp/docs/06-compression.md`
- Go: not yet ported (will use `encoding/binary`-style explicit packing).
