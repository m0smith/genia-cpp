# genia-cpp

**Status: bootstrap only. No C++ interpreter is implemented here yet.**

This is the planned production C++ host for [Genia](https://github.com/m0smith/genia-2026).
It was created by R16 E16-6 (`m0smith/genia-2026#763`) as a repository
shell so the external-host repository boundary is executable before real
C++ implementation work (R20) begins. Nothing in this repository defines
Genia language behavior.

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
| `genia-2026` contract revision | [`b0cf6921d15cea3205e2674117dddea67d7248b8`](https://github.com/m0smith/genia-2026/commit/b0cf6921d15cea3205e2674117dddea67d7248b8) |
| E16-1 adapter-protocol version | `1` |

This is a **pinned-conformance declaration** in the sense E16-4 defines it
(`genia-2026`'s `tools/spec_runner/revision.py`): this repository's
bootstrap was built against that exact `genia-2026` commit. Update this
table deliberately as this repository advances — never silently, and
never by copying semantic behavior instead of the declared revision
identity.

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
  yet — that is R20 (`genia-2026`'s
  [`roadmap/r16-r20.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/r16-r20.md)),
  and it belongs entirely in this repository once it lands.

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
