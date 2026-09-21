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

**E24-1 through E24-6 complete.** E24-1 (`m0smith/genia-2026#955`) built
the toolchain bootstrap and an honest E16-1 adapter skeleton
implementing zero Genia semantics. E24-2 (`m0smith/genia-2026#956`)
added the first real vertical slice: integer literals, one evidenced
bare-name reference (`print`), and `+ - * /` binary expressions, plus
`-c` command mode. E24-3 (`m0smith/genia-2026#957`) widened that to
string/boolean/list literals, assignment, function calls, the native
`map_new`/`map_get`/`map_put`/`map_has?`/`map_remove`/`map_count`/
`map_items`/`utf8_encode` functions, R18 structural/legal-key equality
(`==`), and file-mode CLI. E24-4 (`m0smith/genia-2026#958`) adds map
literals (`{key: value, ...}`), lambdas and closures (including
list/map-destructuring parameter patterns), named-function definitions
with both ordinary and local case/pattern-dispatch bodies, pipelines
(`|>`), the `err(reason[, context])` Outcome constructor and its
`err(...)` rendering, and the one deterministic
`Error: Undefined name: <name>` runtime diagnostic genia-2026's
reference host produces for a genuinely unbound name (never guessed for
a real-but-unimplemented Genia global — see
`src/genia2026_known_globals.hpp`). `map`/`map_acc`/`sum` are
interpreted from a small, genuinely Genia-source-level prelude
installation (`src/global_env.hpp`), not reimplemented in C++, per
`docs/design/r24/native-primitive-inventory.md`'s "list/map prelude
helpers ... interpreted from the shared Genia prelude source" rule.
Anything outside this grammar is rejected at the tokenizer/parser level
and reported `unsupported`, never guessed at (see `src/parser.hpp`).
E24-5 (`m0smith/genia-2026#959`) is a hardening/audit pass, not a new
feature: it adds no new Genia semantics, only an internal adversarial
Catch2 test matrix (`tests/test_diagnostic_boundary.cpp`) plus two real
crash fixes that pass caught by running it — a C++ stack overflow
(undefined behavior, uncatchable by any try/catch) from a few thousand
levels of either Genia-level recursive function calls or parser
nesting (parenthesized grouping, list/map literals/patterns) reliably
segfaulted this adapter before this slice; both are now bounded by a
depth guard well below the measured crash threshold (`src/evaluator.hpp`'s
`kMaxCallDepth`, `src/parser.hpp`'s `kMaxNestingDepth`), converting the
crash into an honest `unsupported` with no change to any
normal-sized program's behavior. There is still no Python code anywhere
in this repository's build or execution path. E24-6
(`m0smith/genia-2026#960`) adds R20 open functions, but only their
*local* (single-module) form: `open name(<pattern>, ...) = <body>`
declares the first clause of a new open interface, and every
subsequent bare top-level `name(<pattern>, ...) = <body>` clause for
that same name (in a contiguous run, exactly like the grouped
case-with-`|` spelling) merges into it. Local dispatch against one
participating unit is identical to E24-4's existing case-dispatch
mechanism, so no new evaluator machinery was needed — only new grammar,
two new portable Core IR node types (`IrOpenFuncDef`, reusing
`IrCaseClause`/`IrPatTuple`/`IrPatLiteral` verbatim per
`docs/design/r20-open-functions-syntax-ir-design.md`), a new integer
`Literal` pattern kind (`open gcd(a, 0) = a`'s `0`), and the `%`
(exact floor-remainder, Python-style) operator the pinned `gcd`
evidence needs. Cross-module `extend`/`use` (contribution/selection)
require `multi_file_eval`, explicitly out of R24 scope per
`docs/design/r24/capability-floor.json`, and are hard-rejected at parse
time rather than attempted. This slice's own preparation found and
fixed two real evidence/guidance gaps in `genia-2026` before and after
implementing: `#971`/`#972` (the pinned `open_functions_r20` bootstrap
case required out-of-scope `multi_file_eval`) and `#973`/`#974` (the
roadmap's own guidance to declare `open_functions` `partial` was itself
wrong — `tools/spec_runner/capabilities.py`'s requires-gate grants zero
evidence credit for `partial`, which would have made the two pinned
cases permanently un-runnable; `supported` is correct here, matching
the same precedent already used for `parser`/`ast_lowering`/
`cli_command_mode`/`cli_file_mode`).

Capabilities declared `supported`: `parser`, `ast_lowering`,
`cli_command_mode`, `cli_file_mode`, `open_functions` (local-only —
cross-module `extend`/`use`, the R20 diagnostic family, and bare
varargs patterns remain genuinely `unsupported` per case, never
fabricated; see `#973`/`#974` for why `supported` rather than `partial`
is the honest declaration here). Declared `partial`: `core_ir_eval`
(exact Integer arithmetic, structural equality, list/map construction,
lambdas/closures, local case/pattern dispatch, pipelines, `err(...)`
Outcomes, and the one deterministic undefined-name diagnostic, over
this slice's grammar only — no Decimal/Rational/Float64 or
`some`/`none`). Every other `spec/manifest.json` capability remains
`unsupported`. Running the full shared spec corpus:
`total=744 passed=73 failed=0 unsupported=671 protocol_error=0 crash=0
timeout=0 invalid=0` (see README.md for the exact case list).

The real C++ host implementation is numbered **R24** (originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). The R24
pre-flight gate recorded **GO** on 2026-09-19 — see `genia-2026`'s
`docs/design/r24-cpp-host-preflight.md` and the four pinned entry
artifacts under `docs/design/r24/` there. E24-1 through E24-6 are the
first six implementation slices of the E24 sequence
(`docs/strategy/roadmap/e24-issue-sequence.md`); **E24-7 (R21-R23 exact
numeric runtime) has not started.**

Known commands:

- setup: none (no package manager; `nlohmann/json` and `Catch2` are
  vendored single headers under `third_party/`)
- build: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- test: `ctest --test-dir build --output-on-failure`
- lint: `clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp && clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp`
- conformance evidence: from a `genia-2026` checkout at the pinned
  revision, `python -m tools.spec_runner --host '<path>/genia-cpp/build/genia-adapter' --evidence evidence.json`
  (expected result at E24-3: `total=744 passed=21 failed=0
  unsupported=723 protocol_error=0 crash=0 timeout=0 invalid=0`)

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
