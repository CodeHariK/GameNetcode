# Netcode from Scratch — concept guides

This is the language-neutral narrative for the project: what each piece of game
netcode is, why it exists, and how it works. The *concepts* are the same in every
language; the concrete code lives in two parallel implementations:

- **NetcodeCpp/** — C++20 (complete, milestones m01–m09), with C++-specific deep
  guides in `NetcodeCpp/docs/`.
- **NetcodeGo/** — Go port, prioritising readability (in progress).

Read the guides in order; each links to the code in both languages.

## Milestones

| # | Concept | C++ | Go |
|---|---------|-----|----|
| 01 | [Sockets & network simulator](01-sockets.md) | done | done |
| 02 | [Virtual connection & reliability](02-connection.md) | done | done |
| 03 | [Channels (delivery guarantees)](03-channels.md) | done | done |
| 04 | [Authoritative server & snapshots](04-snapshots.md) | done | — |
| 05 | [Client prediction & reconciliation](05-prediction.md) | done | — |
| 06 | [Bit packing & delta compression](06-compression.md) | done | — |
| 07 | [Lag compensation (hit rewind)](07-lag-compensation.md) | done | — |
| 08 | [Interest management (AOI)](08-interest-management.md) | done | — |
| 09 | [Network simulator in the live loop](09-network-sim.md) | done | — |

## Run it

    cd NetcodeCpp && make test     # C++: build + run all tests
    cd NetcodeGo  && go test ./... # Go: run all tests

## Two big models (context for all of the above)

Almost everything here is **state synchronization**: an authoritative server owns
the simulation and sends state; clients predict their own entity and interpolate
others. The other major model is **deterministic lockstep / rollback** (send only
inputs; everyone simulates identically) — used by RTS and fighting games. This
project builds the first model; see 05 and 07 for where they touch.
