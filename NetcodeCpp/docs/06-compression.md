# Milestone 6 — Bit packing & delta compression

**What this milestone adds:** bandwidth is the real budget in networked games, so
we make snapshots small — first by packing values into the *minimum* bits, then by
only sending what actually changed.

New files: `src/net/BitStream`, `src/game/DeltaSnapshot`,
`tests/bitstream_test.cpp`, `tests/delta_test.cpp`. `InputBatchHeader` gains
`ack_server_tick`.

## Two techniques, stacked

### 1. Bit packing — `BitStream`
A full `float` is 32 bits and a `bool` is usually 8, but most values don't need
that. `BitWriter`/`BitReader` read and write **arbitrary bit widths**:
- `write_bool` — 1 bit.
- `write_ranged(value, min, max)` — only `bits_required(range)` bits (a value in
  `[0,1000]` costs 10 bits, not 32).
- `write_float(value, min, max, bits)` — **quantizes** a float onto a fixed grid and
  stores it in `bits` bits (e.g. a world position in 16 bits).
- Over-reading past the buffer sets an `overflowed()` flag, so a truncated/malformed
  packet fails safely instead of reading garbage.

### 2. Delta compression — `DeltaSnapshot`
Most of the world doesn't change between two snapshots. So instead of sending every
entity in full, the server sends each entity as a **delta against a baseline** the
client already has: one "changed?" bit per field, and the value only if it changed.
An entity's colour is 24 bits but almost never changes, so it collapses to a single
bit; an entity that didn't move costs almost nothing.

**Choosing the baseline (Quake3-style):** the client tells the server, in each input
batch, the newest snapshot tick it has fully decoded (`ack_server_tick`). The server
deltas the next snapshot against *exactly that* tick. If that baseline has aged out
of history, the server sends an absolute **keyframe** (`baseline_tick == 0`) that any
client can decode. `SnapshotWireHeader` carries `server_tick`,
`last_client_input_tick`, and `baseline_tick`.

## Why quantization is safe here

Quantizing positions is lossy — they snap to a fine grid (~0.003 units). That error
is far smaller than the reconciliation threshold, so it never causes a false
correction, and it's invisible in play. Real engines quantize on the wire too.

## What the tests verify, and why

`bitstream_test`:
- Arbitrary-width round trips (incl. a full 32-bit value), ranged ints with
  clamping, quantized floats within one grid step, and that 100 bools pack into ≤13
  bytes and over-reading flags overflow. *The packer must be exact and safe.*

`delta_test`:
- Absolute (keyframe) round trip, baseline-relative round trip, new/removed
  entities, a measured compression ratio (~85% smaller on a busy scene), and that a
  truncated buffer is rejected. *Encode/decode must be lossless at the field level
  and robust to bad input.*

## Run it

```
make test        # adds bitstream_test, delta_test
make run         # server prints ~24B snapshots; client shows AckedTick advancing
```

## Mental model

Don't mail a fresh full-page form every update. Mail a checklist: tick only the
boxes that changed since the copy the reader already has, and write each number in
the fewest digits it needs.
