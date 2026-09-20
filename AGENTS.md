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

**E24-1 and E24-2 complete.** E24-1 (`m0smith/genia-2026#955`) built the
toolchain bootstrap and an honest E16-1 adapter skeleton implementing
zero Genia semantics. E24-2 (`m0smith/genia-2026#956`) adds the first
real vertical slice on top of it: `genia-adapter` now genuinely parses,
lowers to portable Core IR, and evaluates exactly one deliberately
minimal grammar — integer literals, the one evidenced bare-name
reference (`print`), and `+ - * /` binary expressions with standard
precedence — plus `-c` command mode. Anything outside that grammar is
rejected at the tokenizer/parser level and reported `unsupported`,
never guessed at (see `src/parser.hpp`). There is still no Python code
anywhere in this repository's build or execution path.

Capabilities declared `supported`: `parser`, `ast_lowering`,
`cli_command_mode`. Declared `partial`: `core_ir_eval` (exact Integer
arithmetic over this slice's grammar only — no lists, maps, lambdas,
pattern matching, Decimal/Rational/Float64, file mode, or open
functions). Every other `spec/manifest.json` capability remains
`unsupported`. Running the full shared spec corpus:
`total=740 passed=10 failed=0 unsupported=730 protocol_error=0 crash=0
timeout=0 invalid=0` (see README.md for the exact case list).

The real C++ host implementation is numbered **R24** (originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). The R24
pre-flight gate recorded **GO** on 2026-09-19 — see `genia-2026`'s
`docs/design/r24-cpp-host-preflight.md` and the four pinned entry
artifacts under `docs/design/r24/` there. E24-1 and E24-2 are the first
two implementation slices of the E24 sequence
(`docs/strategy/roadmap/e24-issue-sequence.md`); **E24-3 (lists,
ordered maps, equality, file mode) has not started.**

Known commands:

- setup: none (no package manager; `nlohmann/json` and `Catch2` are
  vendored single headers under `third_party/`)
- build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- test: `ctest --test-dir build --output-on-failure`
- lint: `clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp && clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp`
- conformance evidence: from a `genia-2026` checkout at the pinned
  revision, `python -m tools.spec_runner --host '<path>/genia-cpp/build/genia-adapter' --evidence evidence.json`
  (expected result at E24-2: `total=740 passed=10 failed=0
  unsupported=730 protocol_error=0 crash=0 timeout=0 invalid=0`)

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
