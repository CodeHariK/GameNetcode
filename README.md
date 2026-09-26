# Netcode from Scratch

Two from-scratch UDP game-networking implementations, built milestone by milestone
for learning and a blog series:

- **NetcodeCpp/** — the original C++20 implementation (9 milestones, git-tagged
  m01..m09, deep per-milestone docs in `NetcodeCpp/docs/`).
- **NetcodeGo/** — a Go port, prioritising readability (in progress).

Same concepts in both: non-blocking UDP, a virtual connection with reliability,
channels, authoritative server + snapshots, client prediction & reconciliation,
delta compression, lag compensation, interest management, and a network simulator.

    cd NetcodeCpp && make test     # C++: build + run all tests
    cd NetcodeGo  && go test ./... # Go: run all tests

## Learn the concepts

Start with [`docs/`](docs/README.md) — a language-neutral, milestone-by-milestone
explanation of every piece, with links to the code in both implementations. Each
implementation also has its own deep, language-specific guides (e.g.
`NetcodeCpp/docs/`).
