# genia-cpp AGENTS

This repository is the planned production C++ host for Genia. It
**implements** Genia; it does **not define** Genia.

## Authority order

1. `github.com/m0smith/genia-2026`'s `GENIA_STATE.md` — final authority
   for implemented Genia language/runtime behavior
2. `genia-2026`'s `GENIA_RULES.md`, `GENIA_REPL_README.md`
3. `genia-2026`'s `docs/host-interop/*` (host contract, capability
   registry, porting guide)
4. `genia-2026`'s `docs/architecture/core-ir-portability.md` (the frozen
   minimal portable Core IR boundary)
5. `genia-2026`'s `spec/*` (shared conformance cases) and
   `spec/manifest.json` (capability vocabulary)
6. `genia-2026`'s `docs/design/
   r16-multi-host-conformance-infrastructure-contract.md` (the E16-1
   wire protocol this repository's adapter must speak)

If anything in this repository conflicts with those, they win. This
repository must never fork, copy, or silently reinterpret them as local
truth — read them from `genia-2026` directly.

## Hard rules

- Do not claim any capability is implemented until real code, tests, and
  this repository's own capability-status doc all agree.
- Do not guess at ambiguous portable behavior from Python (`genia-2026`)
  implementation details. If the contract is unclear, stop and raise it
  in `genia-2026` first; resume only once it is clarified there.
- Do not add Genia language semantics, syntax, or Core IR nodes here.
  Any such change is a `genia-2026` contract change, proposed and
  approved there, not invented in this repository.
- Update the pinned `genia-2026` contract revision / E16-1 protocol
  version in `README.md` deliberately, as an explicit, reviewed change —
  never as a side effect of unrelated work.
- Host-local implementation choices (build system, memory model, parser
  internals, toolchain) are this repository's own decision. Observable
  Genia semantics are not — see `genia-2026`'s
  `docs/host-interop/HOST_PORTING_GUIDE.md` for the exact "different
  internals are fine, different observable semantics are not" boundary.

## Status

Bootstrap only (R16 E16-6, `m0smith/genia-2026#763`). No C++ code exists.
`bootstrap/protocol_adapter_stub.py` is a temporary, non-semantic
placeholder proving this repository can participate in the generic
protocol conversation; it is not a starting point for the real
interpreter and should be deleted once R20 lands a genuine C++ adapter.

Known commands:

- setup: TODO (R20)
- build: TODO (R20)
- test: TODO (R20)
- lint: TODO (R20)
