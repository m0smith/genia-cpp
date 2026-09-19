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
interpreter and should be deleted once R24 lands a genuine C++ adapter
(E24-1 in `genia-2026`'s
`docs/strategy/roadmap/e24-issue-sequence.md`).

The real C++ host implementation is now numbered **R24** (originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). The R24
pre-flight gate recorded **GO** on 2026-09-19 — see `genia-2026`'s
`docs/design/r24-cpp-host-preflight.md` and the four pinned entry
artifacts under `docs/design/r24/` there. This is a planning/pre-flight
result only; it does not mean any C++ code exists here yet.

Known commands:

- setup: TODO (E24-1)
- build: TODO (E24-1)
- test: TODO (E24-1)
- lint: TODO (E24-1)

## Dependency/toolchain policy (pinned by the R24 pre-flight)

Decided in `genia-2026`'s `docs/design/r24/dependency-toolchain-policy.md`;
reproduced here for local reference. Do not edit this table without
updating that file too — it is the authoritative copy.

| Concern | Decision |
|---|---|
| Build system | CMake |
| C++ language version | C++20 |
| Compiler support policy | GCC and Clang, latest two major versions, Linux; no MSVC commitment at R24 |
| Package/dependency management | Vendored/header-only or pinned git submodules only; no Conan/vcpkg at R24 |
| Test framework | Catch2 (single header) |
| Formatting | `clang-format`, pinned config |
| Lint/static analysis | `clang-tidy`, pinned config, run in the local/self-hosted conformance job |
| JSON protocol handling | `nlohmann/json`, used only at the E16-1 adapter boundary |
| Arbitrary-precision Integer | In-house bignum (sign + base-2^32 limb vector) |
| Decimal | In-house coefficient (bignum) + exponent pair |
| Rational | In-house numerator/denominator pair over the Integer bignum |
| Float64 | Native `double`, boxed as an explicit tagged runtime value |
| Unicode strategy | In-house UTF-8 decode/code-point iteration; no ICU |
| Ordered-map representation | In-house insertion-ordered map (vector of pairs + hash index) |
| Diagnostic representation | In-house struct mirroring the R19 portable diagnostic schema; C++ exceptions caught and normalized before crossing the adapter boundary |
| CI/conformance invocation | Local/self-hosted `cmake --build` + `ctest` + `python -m tools.spec_runner --host` with a committed evidence JSON; no large hosted matrix |
