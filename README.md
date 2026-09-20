# genia-cpp

**Status: bootstrap only. No C++ interpreter is implemented here yet.**

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
| `genia-2026` contract revision | [`b859ddc6374d8c930180ef546b103cb2fa1c0d9b`](https://github.com/m0smith/genia-2026/commit/b859ddc6374d8c930180ef546b103cb2fa1c0d9b) |
| E16-1 adapter-protocol version | `1` |
| Represents | `genia-2026` `main` at the time the R24 pre-flight gate recorded GO (2026-09-19): R16-R23 complete and audited, R24 pre-flight artifacts (`docs/design/r24-cpp-host-preflight.md` and `docs/design/r24/*`) pinned, P8/P9 provider-composition proofs landed but explicitly do not change R24 scope. |

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

- `bootstrap/protocol_adapter_stub.py` — a minimal, self-contained
  placeholder that speaks just enough of the E16-1 protocol to answer a
  `capabilities` request (declaring the pinned revision/protocol version
  above and every capability `unsupported`) and to answer every other
  operation with a deterministic `unsupported` response. **It does not
  parse, lower, or evaluate any Genia source — it does not interpret
  Genia at all.** It exists only to prove this repository can
  participate in the generic protocol conversation (run through
  `genia-2026`'s `tools/spec_runner --host`) without consulting Python
  implementation source. It has no dependency on `genia-2026`'s Python
  package at runtime, matching the repository-boundary rule that this
  repo owns its own toolchain.
- Nothing else. No C++ build system, lexer, parser, or evaluator exists
  yet — that is R24 (`genia-2026`'s
  [`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md),
  starting with E24-1's toolchain bootstrap), and it belongs entirely in
  this repository once it lands.

## Trying the bootstrap placeholder

```bash
git clone https://github.com/m0smith/genia-2026
git clone https://github.com/m0smith/genia-cpp
python -m tools.spec_runner --host 'python3 ../genia-cpp/bootstrap/protocol_adapter_stub.py'
# (run from inside the genia-2026 checkout; every applicable case reports
# UNSUPPORTED, honestly, since no interpreter exists here yet)
```

## Contributing

Read `AGENTS.md` before making any change here.
