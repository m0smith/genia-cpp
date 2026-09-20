# genia-cpp

**Status: E24-1 toolchain bootstrap complete. No Genia semantics are
implemented. `genia-adapter` truthfully declares every capability
`unsupported`.**

This is the planned production C++ host for [Genia](https://github.com/m0smith/genia-2026).
It was created by R16 E16-6 (`m0smith/genia-2026#763`) as a repository
shell so the external-host repository boundary is executable before real
C++ implementation work begins. Nothing in this repository defines
Genia language behavior.

Real C++ implementation work is now numbered **R24** (it was originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). As of
2026-09-19 the R24 pre-flight gate
([`docs/design/r24-cpp-host-preflight.md`](https://github.com/m0smith/genia-2026/blob/main/docs/design/r24-cpp-host-preflight.md)
in `genia-2026`) recorded **GO**, and a dependency-ordered implementation
ticket sequence exists
([`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md)).
No C++ code exists in this repository yet.

## Authority

[`m0smith/genia-2026`](https://github.com/m0smith/genia-2026) is the sole
authority for:

- the Genia language contract and Core IR portability boundary
- shared conformance specs (`spec/`)
- the generic conformance runner (`tools/spec_runner`)
- the E16-1 host-adapter wire protocol
  ([contract](https://github.com/m0smith/genia-2026/blob/main/docs/design/r16-multi-host-conformance-infrastructure-contract.md))

This repository **implements** Genia; it never **defines** Genia. It does
not fork or copy `GENIA_STATE.md`, `GENIA_RULES.md`, `GENIA_REPL_README.md`,
Core IR docs, or shared specs as editable local truth — read them from
`genia-2026`.

When this host's behavior would disagree with shared evidence: fix the
host if the `genia-2026` contract is clear; if the contract is ambiguous,
stop guessing and clarify it upstream in `genia-2026` first, then resume
here. See `genia-2026`'s
[`multi-host-conformance-policy.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/multi-host-conformance-policy.md).

## Pinned contract

| | |
|---|---|
| `genia-2026` contract revision | [`41e5d80b79f1b05d6836a489ff70ba4f751e67e9`](https://github.com/m0smith/genia-2026/commit/41e5d80b79f1b05d6836a489ff70ba4f751e67e9) |
| E16-1 adapter-protocol version | `1` |
| Represents | `genia-2026` `main` after PR `#953` merged the R24 pre-flight artifacts (`docs/design/r24-cpp-host-preflight.md`, `docs/design/r24/*`, and `docs/strategy/roadmap/e24-issue-sequence.md`): R16-R23 complete and audited, R24 pre-flight recorded GO (2026-09-19). This is the exact revision `src/protocol.hpp`'s `kContractRevision` declares and `genia-adapter`'s `capabilities` response reports. |

This is a **pinned-conformance declaration** in the sense E16-4 defines it
(`genia-2026`'s `tools/spec_runner/revision.py`): this repository's
bootstrap was built against that exact `genia-2026` commit. Update this
table deliberately as this repository advances — never silently, and
never by copying semantic behavior instead of the declared revision
identity.

This revision was chosen deliberately as the post-R23 contract baseline
R24 implementation will actually target, not blindly advanced to whatever
`genia-2026/main` happened to be at some other moment: it was verified to
be `genia-2026`'s current `origin/main` tip at the time of this refresh,
with R16 through R23 all showing a recorded skeptical release-truth-audit
PASS. If `genia-2026/main` moves again before E24-1 lands, do not
silently re-pin to the new tip — re-verify that R16-R23 remain complete
and that no new prerequisite work has landed, then update this table in
its own reviewed change.

## What exists here

E24-1 (`m0smith/genia-2026#955`) replaced the temporary Python
`bootstrap/protocol_adapter_stub.py` placeholder with a real, compiled
C++ E16-1 adapter. That file has been deleted; there is no Python code
left in this repository's execution path.

- `src/protocol.hpp` — the E16-1 wire-envelope helpers (request/response
  shape, the pinned capability vocabulary, the `capabilities` and
  `unsupported` response builders). Transport-only; no Genia semantics.
- `src/adapter.hpp` — request classification/dispatch, factored out of
  `main()` so it is unit-testable without spawning a subprocess.
- `src/main.cpp` — the binary entry point: reads exactly one JSON request
  from stdin, writes exactly one JSON response to stdout, and nothing
  else. Every internal exception is caught here (diagnostic-normalization
  boundary discipline) so a malformed request never crashes the process.
- `tests/test_protocol.cpp` — Catch2 unit tests for the envelope/dispatch
  logic (internal genia-cpp tests, not shared conformance evidence).
- `third_party/nlohmann_json/json.hpp`, `third_party/catch2/catch.hpp` —
  vendored single-header dependencies per the R24 dependency/toolchain
  policy (pinned versions: nlohmann/json v3.11.3, Catch2 v2.13.10).
- `genia-adapter` (the built binary) truthfully declares **every**
  capability in `genia-2026`'s `spec/manifest.json` (required + optional,
  30 names at the pinned revision) as `unsupported`, and answers `parse`/
  `lower`/`eval`/`cli` with a deterministic `unsupported` response. It
  parses, lowers, and evaluates no Genia source whatsoever.
- No parser, lowering, or evaluator exists yet — that starts at E24-2
  (`genia-2026`'s
  [`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md)),
  which is **not** part of this repository's current state.

## Building and running the adapter

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure   # internal unit tests
```

```bash
git clone https://github.com/m0smith/genia-2026
git clone https://github.com/m0smith/genia-cpp
cd genia-cpp && cmake -S . -B build && cmake --build build && cd ..
cd genia-2026
python -m tools.spec_runner --host '../genia-cpp/build/genia-adapter' --evidence evidence.json
# every applicable case reports UNSUPPORTED, honestly: 0 capabilities are
# claimed supported, 0 fail/crash/protocol_error/timeout.
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
