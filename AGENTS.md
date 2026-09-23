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

**E24-1 through E24-7 complete. E24-8 remains.** E24-1
(`m0smith/genia-2026#955`) built
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

E24-7 (`m0smith/genia-2026#961`) is a large ticket (R21 source
classification + R22 exact numeric runtime + R23 rendering/format-spec/
JSON boundary) implemented as a sequence of coherent increments rather
than one change, matching this project's established per-slice
discipline. **Increment 1** adds: unary minus (`-<expr>`, a real
`ast::Kind::Unary`/`core_ir::Kind::Unary` node — genia-2026's own
`Unary`/`IrUnary`, not a special case of Binary), and R21 Decimal
source-literal classification/canonicalization/lowering (`1.25`,
`1e3`, `1.25e-2`, equivalent-spelling normalization, rejected leading-/
trailing-dot and malformed-exponent forms — `docs/design/r21-numeric-
source-portable-representation-contract.md`), plus just enough Decimal
runtime support to close the loop end to end for a literal: a `Decimal`
value kind (coefficient `bignum::Integer` + `int64_t` exponent),
negation, and canonical display/debug rendering (R23 section 2.2's
fixed/scientific notation rule, verified directly against
`src/genia/numeric_runtime.py`'s `_canonical_decimal_text`). Decimal
arithmetic/comparison beyond negation, Rational, Float64, the format-
spec engine, and the JSON boundary all remain further increments.
Enabling Decimal literals immediately exposed a real, pre-existing gap
this slice's own full-corpus run caught before merging: R18's
Integer/Decimal numeric-equality bridge (`1 == 1.0`,
`docs/design/r18-portable-value-equality-contract.md`'s "Numeric
equality" table) was unimplemented, silently turning two previously-
honestly-`unsupported` `spec/eval/r18-*.yaml` cases into wrong `ok`
results; `equality.hpp`'s `decimal_equals_integer` fixes this via exact
(never lossy-float) comparison, since Decimal is always exact.

**Increment 2** adds R22 section 3's exact Rational runtime value
(`bignum::Integer` numerator/denominator, gcd-reduced, denominator
positive, sign carried by the numerator, denominator-one collapsing to
Integer — `src/bignum.hpp`'s new `Integer::gcd`, `src/rational.hpp`'s
`construct_rational`), the `rational(numerator, denominator)`
constructor (both arguments must be Integers; a zero denominator is
honestly unsupported, matching `1 / 0`'s existing convention — no
diagnostic-worthy error path exists yet), canonical `<numerator>/
<denominator>` rendering (R23 section 2.3), and the `!=` operator
(exactly the logical negation of `==`, verified directly against the
reference host — R18: "equality is ONE relation (==, !=)"). This
increment also generalizes `equality.hpp`'s cross-kind numeric bridge
from increment 1's ad hoc Integer/Decimal-only
`decimal_equals_integer` into R22 section 10.1's full "Exact family"
rule (Integer/Decimal/Rational compare by mathematical value in every
pairing), by converting each exact-family value to an exact numerator/
denominator pair and comparing via cross-multiplication — never
rounding through a host binary float. Rational arithmetic (`+ - * /
%`), comparison operators (`< <= > >=`, which do not exist in this
slice's grammar at all yet), Float64, the format-spec engine, and the
JSON boundary all remain further increments.

**Increment 3** adds R22 sections 6-8's exact-family arithmetic
(`src/arithmetic.hpp`) for `+`, `-`, `*`, `/`, and `%` across
Integer/Decimal/Rational, closing the gap increment 2 left open.
`+`/`-`/`*` follow section 6's `Integer < Decimal < Rational`
promotion lattice: Integer/Integer keeps the existing fast bignum
path; Decimal participation with no Rational operand aligns exponents
and canonicalizes, always staying Decimal even for a mathematically
integral result (`1.5 + 0.5 == 2.0`, never collapsing to Integer, per
the contract's explicit rule); any Rational participation goes through
general numerator/denominator fraction algebra reduced by
`rational::construct_rational`, collapsing to Integer only at
denominator 1 (`rational(1, 2) + rational(1, 2) == 1`). `/` implements
section 7's own table, not section 6's: Integer/Integer division that
is not evenly divisible produces Rational, *never* Decimal even when
the quotient would terminate in base 10 (`1 / 2 == rational(1, 2)`);
Decimal participation with no Rational operand produces Decimal only
when the reduced quotient's denominator has no prime factors other
than 2 and 5 (`denominator_terminates_in_base10`), Rational otherwise
(`1.0 / 2 == 0.5` but `1.0 / 3 == rational(1, 3)`). `%` reuses section
6's promotion rule (not section 7's), per section 8:
`q = floor(left / right); left % right = left - q*right`, computed via
`bignum::Integer::floor_remainder` for the floor quotient and exact
fraction subtraction for the result — Decimal-only `%` never needs a
termination check because its denominator is always a divisor of a
power of 10 by construction. Division/remainder by exact zero stays
honestly `unsupported` (deterministic numeric misuse this slice still
has no diagnostic-worthy error path for), matching every other
zero-divisor convention already established (`1 / 0`,
`rational(1, 0)`). No capability declaration changed (`core_ir_eval`
stays `partial`).

**Increment 4** adds R22 section 10.1's ordered comparison operators
(`<`, `<=`, `>`, `>=`) for the exact family (Integer/Decimal/Rational),
via a new `equality.hpp` function (`exact_family_compare`) that reuses
`exact_family_equal`'s numerator/denominator cross-multiplication
technique for ordering, safe because every exact-family kind's
converted denominator is always strictly positive (Integer: 1;
Decimal: a positive power of 10; Rational: positive by
canonicalization), so no sign-flip caveat applies. This increment also
fixes a latent parser conformance bug found while adding the new
operator's precedence tier: `parser.hpp`'s `parse_expr` previously
flattened `+`/`-` and `==`/`!=` into one precedence level, contrary to
`src/genia/parser.py`'s own `PRECEDENCE` table (`EQEQ`/`NE` = 30 <
`PLUS`/`MINUS` = 50) — e.g. `1 == 2 + 3` must group as `1 == (2 + 3)`,
not `(1 == 2) + 3`. The bug never surfaced as a silently wrong `ok`
result (a Boolean/Integer arithmetic mix like `(1 == 2) + 3` was
already `unsupported` by this slice's exact-family-only arithmetic
either way), but is fixed now via a proper precedence-climbing chain
(`parse_expr` -> `parse_equality` (30) -> `parse_comparison` (40, the
new tier) -> `parse_additive` (50) -> `parse_term` (60)), verified
directly against the reference parser's own table, not guessed at.
Comparing a non-exact-family operand (e.g. `true < false`,
`"a" < "b"`) remains honestly `unsupported`, matching R22's own scope
(section 10.1 covers only the exact family; string/Boolean ordering is
not part of this contract). The one pinned
`r22-exact-family-and-float64-ordering.yaml` case also exercises
`float64(...)`, a further increment, so it does not yet flip to
passing in the shared corpus -- this increment's own new Catch2 tests
cover exactly its Integer/Decimal/Rational subset instead. No
capability declaration changed.

**Increment 5** adds R22 sections 4-5's boxed Float64 value and explicit
`float64(value)` / `exact(value)` conversions. Exact-to-binary64 conversion
uses integer numerator/denominator arithmetic for exact round-to-nearest,
ties-to-even decisions; overflow produces the normalized contract diagnostic
and exact zero produces positive zero. Binary64-to-exact decodes the bits
directly into the represented dyadic Decimal, including canonical Decimal
zero for either signed zero. R23's canonical constructor-shaped renderer
preserves signed zero and shortest-roundtrip finite text. Float64 arithmetic,
the exact/Float64 comparison and map-key bridge, format specs, and JSON remain
later increments. No capability declaration changed.

**Increment 6** adds R22 section 9's closed-domain Float64 arithmetic:
unary `-` and Float64-with-Float64 `+`, `-`, `*`, `/`, and `%`. Ordinary
operations use native binary64; `%` adjusts `fmod` to the contract's floor
remainder (including divisor-signed zero), rather than exposing C++'s
truncating remainder. Either signed-zero divisor produces the exact normalized
division/remainder diagnostic. Any Float64/exact-family pairing remains
rejected in both directions for every binary arithmetic operator; this does
not place Float64 in the exact promotion lattice. Equality, ordering, and map
keys remain later work. No capability declaration changed.

**Increment 7** adds R22 sections 10.2-10.3's Float64 comparison and
numeric map-key bridge. Finite binary64 operands are decoded to their exact
represented dyadic numerator/denominator before comparison, so exact operands
are never rounded through `double`; the same relation drives `==`/`!=`,
`< <= > >=`, and reduced-fraction map-key identity across Integer/Decimal/
Rational/Float64. Signed zeros compare equal, infinities retain IEEE value
ordering/equality, and NaN is unequal/unordered and rejected as a key.
Replacement, lookup, membership, and removal all use that one key relation
while preserving insertion order. **Increment 8** adds R23 numeric field-format
specs over the existing canonical renderer: alignment/width, sign-aware zero
padding and locale-independent grouping for plain numeral atoms, and exact
decimal half-up `.n` precision for Integer/Decimal/Rational/Float64. Decimal
and Rational never pass through binary floating point; Float64 precision starts
from its exact decoded dyadic ratio. Unsupported representation/spec pairings
use normalized `format-error` diagnostics. First-class `Format(...)` values and
JSON remain later work. No capability declaration changed.

**Increment 9** adds the strict R23 numeric JSON boundary required by the six
shared `r23-json-*` eval cases. Integer encode/decode enforces the R9 safe
interval; Decimal stability is decided by exact conversion and exact comparison
against the existing shortest-roundtrip Float64 spelling; Rational encoding
requires an exact terminating Decimal that passes the same stability predicate;
finite Float64 emits the existing canonical inner spelling and non-finite values
are rejected. Fraction/exponent decode is lexical and never materializes through
binary64. The increment also adds only the narrow success-Option,
JSON-representation, `unwrap_or`, `representation_match`, `display`, and
`IrPatErr` support those six shared cases require. Compatibility JSON remains
unsupported.

Increment 10 closes the two remaining required `spec/eval/r22-*.yaml`
gaps: normalized mixed exact/Float64 `none("type-error", context)` outcomes,
and the numeric-literal-only quote/quasiquote/metacircular-eval plus Decimal
literal-pattern surface. No capability declaration changed.

Capabilities declared `supported`: `parser`, `ast_lowering`,
`cli_command_mode`, `cli_file_mode`, `open_functions` (local-only —
cross-module `extend`/`use`, the R20 diagnostic family, and bare
varargs patterns remain genuinely `unsupported` per case, never
fabricated; see `#973`/`#974` for why `supported` rather than `partial`
is the honest declaration here). Declared `partial`: `core_ir_eval`,
`prelude_autoload` (only the bounded source-level prelude required by the floor),
and `shared_spec_runner` (matching the Python adapter's partial precedent).
`core_ir_eval` covers
(the full R22 section 6-8 exact-family arithmetic/division/
floor-remainder promotion rules for Integer/Decimal/Rational, section
10.1's ordered-comparison (`< <= > >=`) bridge alongside `==`/`!=`,
`rational(...)` construction/display, structural equality, list/map
construction, lambdas/closures, local case/pattern dispatch,
pipelines, `err(...)` Outcomes, the one deterministic undefined-name
diagnostic, explicit Float64 conversions/rendering/arithmetic, and the
Float64 comparison/numeric-key bridge, numeric field-format specs, and the six-case
strict R23 numeric JSON boundary — no first-class `Format(...)` value,
compatibility JSON, or general `some`/`none`). Every other
`spec/manifest.json` capability remains
`unsupported`. Running the full shared spec corpus: `total=755
passed=141 failed=0 unsupported=614 protocol_error=0 crash=0 timeout=0
invalid=0`; increment 8 adds the R23 numeric field-format cases and the older
field-spec cases that use the same implemented surface; increment 9 adds all six
shared `r23-json-*` cases; increment 10 adds the two remaining required R22
eval cases.

The real C++ host implementation is numbered **R24** (originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). The R24
pre-flight gate recorded **GO** on 2026-09-19 — see `genia-2026`'s
`docs/design/r24-cpp-host-preflight.md` and the four pinned entry
artifacts under `docs/design/r24/` there. E24-1 through E24-6 are the
first six implementation slices of the E24 sequence
(`docs/strategy/roadmap/e24-issue-sequence.md`); **E24-7 (R21-R23 exact
numeric runtime and interchange) is complete after ten bounded increments.**

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
