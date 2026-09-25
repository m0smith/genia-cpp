# genia-cpp

**Status: R24 complete through E24-8; R25 E25-2 Cell implemented on a
temporary stack above `genia-cpp` PR #21 and `genia-2026` PR #1007.
`genia-adapter` implements integer/string/boolean/list/map literals,
Decimal source literals (`1.25`, `1e3`, ...), the exact Rational
runtime value and `rational(numerator, denominator)` construction
(gcd-reduced, canonical `<numerator>/<denominator>` display), unary
minus, bare-name references, assignment, `+ - * / == != % < <= > >=`
binary expressions -- including the full R22 exact-family
(Integer/Decimal/Rational) arithmetic promotion lattice, exact
division, exact floor-remainder, and mathematical-value ordering
(sections 6-8, 10.1), not just plain Integer/Integer -- lambdas/closures, named functions
(ordinary and local case/pattern-dispatch bodies), local R20 open
functions (grouped and repeated top-level clause spellings, single
module only), pipelines (`|>`), the `err(...)` Outcome constructor and
its rendering, the R22 exact-family (Integer/Decimal/Rational)
numeric-equality bridge, one deterministic undefined-name runtime
error, boxed Float64 values with explicit `float64(...)` / `exact(...)`
conversion, canonical rendering, exact represented-value comparison, and
cross-kind numeric map-key identity, the strict R23 numeric JSON boundary
covered by the six shared E24-7 JSON cases, calls to the native
`map_*`/`utf8_encode`/`err`/`sum` functions,
and `-c`/file-mode CLI -- end to end (source -> parser -> portable Core
IR -> evaluator -> normalized adapter result), hardened against a C++
stack-overflow crash from adversarially deep recursion/nesting (E24-5).
The `refs` capability now implements creation, blocking get, set, set-state
inspection, atomic update, and identity equality. Cell, Process, and every
other later Genia behavior remain honestly `unsupported`.**

This is the bounded production C++ host for [Genia](https://github.com/m0smith/genia-2026).
It was created by R16 E16-6 (`m0smith/genia-2026#763`) as a repository
shell so the external-host repository boundary is executable before real
C++ implementation work began. Nothing in this repository defines
Genia language behavior.

Real C++ implementation work is numbered **R24** (it was originally
planned as R21; `genia-2026` planning issue #845 decomposed the Exact
Numeric Model into R21-R23 and moved the C++ host to R24). The R24
pre-flight gate
([`docs/design/r24-cpp-host-preflight.md`](https://github.com/m0smith/genia-2026/blob/main/docs/design/r24-cpp-host-preflight.md)
in `genia-2026`) recorded **GO** on 2026-09-19, and a dependency-ordered
implementation ticket sequence exists
([`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md)).
E24-1 through E24-8 are complete. The skeptical completion audit records PASS
in `genia-2026/docs/analysis/r24-release-truth-audit.md`.

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
Both E24-2's and E24-3's own preparation found and fixed gaps upstream
before implementing:

- `m0smith/genia-2026#963`/`#964` (E24-2): a bootstrap evidence case
  required out-of-scope pattern dispatch.
- `#965`/`#966` (E24-2): the generic external-host `cli`-category path
  never stripped trailing newlines, unlike the in-process Python-host
  path every `spec/cli/*.yaml` case's expected output assumes.
- `#968`/`#969` (E24-3): three of the four pinned E24-3 bootstrap
  categories cited cases requiring E24-4-scope pattern dispatch/
  recursion or E24-7-scope Decimal numbers.
- `#971`/`#972` (E24-6): the pinned `open_functions_r20` bootstrap case
  required out-of-R24-scope `multi_file_eval` (cross-module dispatch).
- `#973`/`#974` (E24-6): the roadmap's own guidance that `open_functions`
  must be declared `partial` (not `supported`) was itself wrong --
  `tools/spec_runner/capabilities.py`'s requires-gate grants zero
  evidence credit for `partial`, which would have made the two pinned
  `open_functions_r20` cases permanently un-runnable.

## Pinned contract

| | |
|---|---|
| `genia-2026` contract revision | [`02f4caa15371f3ab6670de446cc4150debfe318e`](https://github.com/m0smith/genia-2026/commit/02f4caa15371f3ab6670de446cc4150debfe318e) |
| E16-1 adapter-protocol version | `1` |
| Represents | E25-0 PR #1007 head: portable Ref/Cell/local Process contract and deterministic R16 causal evidence. This is the exact revision `src/protocol.hpp` declares. This C++ branch is stacked and must not merge before #1007. |

This is a **pinned-conformance declaration** in the sense E16-4 defines it
(`genia-2026`'s `tools/spec_runner/revision.py`): this repository's
implementation was built against that exact `genia-2026` commit. Update
this table deliberately as this repository advances — never silently, and
never by copying semantic behavior instead of the declared revision
identity.

## What exists here

E24-4 (`m0smith/genia-2026#958`) widens E24-3's vertical slice to cover
Outcome values, lambdas/closures, local case/pattern dispatch,
pipelines, and one deterministic runtime-error diagnostic. E24-6
(`m0smith/genia-2026#960`) adds R20 open functions, but only their
*local* (single-module) form: `open name(<pattern>, ...) = <body>`
declares the first clause of a new open interface, and every
subsequent bare top-level `name(<pattern>, ...) = <body>` clause for
that same name -- in a contiguous run, exactly like the grouped
case-with-`|` spelling -- merges into it. Local dispatch is identical
to the existing E24-4 case-dispatch mechanism (one participating unit,
first-match-in-source-order), so no new evaluator machinery was added,
only new grammar and two new portable Core IR node types
(`IrOpenFuncDef`, reusing `IrCaseClause`/`IrPatTuple`/`IrPatLiteral`
verbatim) plus the `%` (exact floor-remainder) operator the pinned
`gcd` evidence needs. Cross-module `extend`/`use` (contribution/
selection) require `multi_file_eval`, explicitly out of R24 scope per
`docs/design/r24/capability-floor.json` -- both are hard-rejected at
parse time (never silently misparsed as ordinary identifiers) rather
than attempted. E24-7 (`m0smith/genia-2026#961`) is a large ticket
(R21 source classification + R22 exact numeric runtime + R23
rendering/format-spec/JSON boundary) built as a sequence of increments;
**increment 1** adds unary minus (a real `IrUnary` node, not a special
case of Binary) and R21 Decimal source-literal classification/
canonicalization/lowering (`1.25`, `1e3`, `1.25e-2`, equivalent-
spelling normalization, rejected leading-/trailing-dot and malformed-
exponent forms), plus just enough Decimal runtime support to close the
loop for a literal end to end: a `Decimal` value kind, negation, and
canonical display/debug rendering (R23 section 2.2's fixed/scientific
rule). Enabling Decimal literals immediately exposed a real gap this
slice's own full-corpus run caught before merging: R18's Integer/
Decimal numeric-equality bridge (`1 == 1.0`) was unimplemented, which
would have silently turned two previously-honestly-`unsupported`
`spec/eval/r18-*.yaml` cases into wrong `ok` results -- fixed via exact
(never lossy-float) comparison, since Decimal is always exact.
**Increment 2** adds R22 section 3's exact Rational runtime value
(`bignum::Integer` numerator/denominator, gcd-reduced via a new
`Integer::gcd`, denominator positive, denominator-one collapsing to
Integer -- `src/rational.hpp`'s `construct_rational`), the
`rational(numerator, denominator)` constructor, canonical
`<numerator>/<denominator>` rendering (R23 section 2.3), and `!=` (the
logical negation of `==`, R18: "equality is ONE relation (==, !=)").
`equality.hpp`'s cross-kind numeric bridge is generalized from
increment 1's Integer/Decimal-only special case into R22 section 10.1's
full "Exact family" rule: Integer/Decimal/Rational compare by
mathematical value in every pairing, via converting each to an exact
numerator/denominator pair and comparing by cross-multiplication --
never rounding through a host binary float. **Increment 3** adds R22
sections 6-8's exact-family arithmetic (`src/arithmetic.hpp`):
`+`/`-`/`*` follow the `Integer < Decimal < Rational` promotion
lattice (Integer/Integer stays Integer; Decimal participation retains
Decimal even for a mathematically integral result, e.g. `1.5 + 0.5 ==
2.0`, never collapsing to Integer; any Rational participation produces
a reduced Rational, collapsing to Integer only at denominator 1, e.g.
`rational(1, 2) + rational(1, 2) == 1`); `/` implements section 7's own
table (Integer/Integer division that is not evenly divisible produces
Rational, *never* Decimal, even when the quotient would terminate in
base 10 -- `1 / 2 == rational(1, 2)`, not `0.5`; Decimal participation
with no Rational operand produces Decimal only when the exact quotient
terminates in base 10, Rational otherwise -- `1.0 / 2 == 0.5` but
`1.0 / 3 == rational(1, 3)`); `%` follows the same section-6 promotion
rule as `+`/`-`/`*` (before Rational denominator-one collapse), per
section 8's `q = floor(left / right); left % right = left - q * right`.
Division/remainder by exact zero remains honestly `unsupported`
(deterministic numeric misuse this slice has no diagnostic-worthy error
path for yet), matching every other zero-divisor convention already
established. **Increment 4** adds R22 section 10.1's ordered comparison
operators (`<`, `<=`, `>`, `>=`) for the exact family, reusing
`exact_family_equal`'s numerator/denominator cross-multiplication
technique for ordering (`equality.hpp`'s new `exact_family_compare`) --
safe because every exact-family kind's converted denominator is always
strictly positive, so cross-multiplication never needs a sign-flip
caveat. Adding this new precedence tier also surfaced and fixed a
latent parser bug: `parser.hpp`'s expression grammar previously
flattened `+`/`-` and `==`/`!=` into one precedence level, contrary to
`src/genia/parser.py`'s own `PRECEDENCE` table (`EQEQ`/`NE` = 30 <
`PLUS`/`MINUS` = 50) -- e.g. `1 == 2 + 3` must group as `1 == (2 + 3)`,
not `(1 == 2) + 3`. This never produced a silently wrong `ok` result
(the mis-grouped `(1 == 2) + 3` shape is itself `unsupported`, a
Boolean/Integer arithmetic mix outside this slice's exact-family-only
arithmetic either way), but is fixed now via a proper
precedence-climbing chain (`parse_expr` -> `parse_equality` (30) ->
`parse_comparison` (40, new) -> `parse_additive` (50) -> `parse_term`
(60)). Comparing a non-exact-family operand (`true < false`, `"a" <
"b"`) remains honestly `unsupported` -- R22 section 10.1 covers only
the exact family. Increment 5 adds the boxed Float64 value, explicit
`float64(value)` / `exact(value)` conversion, exact ties-to-even rounding and
overflow rejection, direct bit-decoding to the represented dyadic Decimal,
and R23 canonical rendering including signed zero. Increment 6 adds unary and
binary Float64 arithmetic with floor remainder, normalized zero-divisor
diagnostics, and strict rejection of mixed exact/Float64 arithmetic. Increment 7
adds exact represented-value Float64 equality/ordering and one reduced-fraction
numeric map-key identity across all four numeric kinds. Increment 8 adds R23
numeric field-format specs: alignment/width on canonical text; sign-aware
zero-padding and locale-independent grouping for plain numeral atoms; exact
decimal half-up precision for Integer, Decimal, Rational, and Float64; and
normalized rejection of unsupported representation/spec combinations.
Increment 9 adds the six-case strict R23 numeric JSON boundary: safe Integer
numbers, exact stable Decimal encode/lexical decode, terminating-and-stable
Rational encode, canonical finite Float64 encode, normalized rejection, and
the required narrow success-Option/JSON-representation plumbing. Compatibility
JSON. Increment 10 completed the remaining required R22 shared evidence and final hardening:

- `src/protocol.hpp` — the E16-1 wire-envelope helpers, plus the
  per-capability status overrides (`parser`/`ast_lowering`/
  `cli_command_mode`/`cli_file_mode`/`open_functions` `supported`,
  `core_ir_eval` `partial`) this slice earned with real evidence.
- `src/adapter.hpp` — request classification/dispatch for
  `capabilities`/`parse`/`lower`/`eval`/`cli` (including both `-c`
  command mode and bare-file-path file mode), routing the latter four
  through `src/engine.hpp`.
- `src/parser.hpp`, `src/ast.hpp`, `src/pattern.hpp` — a tokenizer and
  recursive-descent parser for exactly this slice's grammar:
  integer/string/boolean/list/map literals, assignment, named-function
  definitions (`name(params) = body`, ordinary or local
  case/pattern-dispatch), local R20 open functions (`open name(<pattern>,
  ...) = body` and contiguous repeated bare clauses, per-argument
  pattern grammar reused verbatim, including integer-literal patterns
  like `open gcd(a, 0) = a`), lambdas (`(params) -> body`, including
  list/map-destructuring parameters), pipelines (`|>`), general
  parenthesized grouping, function calls, the bare-name references
  every other identifier now resolves to (whether a name is actually
  *bound* is a runtime concern -- see `src/evaluator.hpp` -- not a
  parse-time restriction, matching genia-2026's real grammar), and
  `+ - * / == != % < <= > >=` binary expressions via a real
  precedence-climbing chain matching `src/genia/parser.py`'s own
  `PRECEDENCE` table exactly (`EQEQ`/`NE` = 30 < `LT`/`LE`/`GT`/`GE` =
  40 < `PLUS`/`MINUS` = 50 < `STAR`/`SLASH`/`PERCENT` = 60, higher
  binds tighter -- `parse_expr` -> `parse_equality` -> `parse_comparison`
  -> `parse_additive` -> `parse_term`; E24-7 increment 4 fixed a latent
  bug where `+`/`-` and `==`/`!=` were previously flattened into one
  level) (`%` is exact floor-remainder, Python-style, sign follows the
  divisor -- verified directly against `src/genia/numeric_runtime.py`'s
  `exact_remainder`, not C++'s native truncating `%`; `!=` is exactly
  the logical negation of `==`, R18: "equality is ONE relation
  (==, !=)"), Decimal source
  literals (R21 section 2's `DIGIT+ "." DIGIT+`/`DIGIT+ exponent`/
  `DIGIT+ "." DIGIT+ exponent` forms -- classified in the tokenizer, not
  guessed at by pattern-matching the digit run after the fact), and
  unary minus (a real prefix operator binding tighter than `* / %`,
  matching genia-2026's own `Unary` AST node -- source sign is never
  part of a numeric literal itself, R21's "-1.25 is unary minus applied
  to the positive Decimal literal" rule). `pattern.hpp`
  holds the one pattern-shape struct
  (Bind/Wildcard/Rest/List/Map/Tuple/Literal) shared by the parser and
  evaluator, matching `docs/architecture/core-ir-portability.md`'s named
  pattern families. Increment 10 additionally accepts only numeric-literal
  `quote(...)`/`quasiquote(...)` forms and lowers them through the approved
  `IrQuote`/`IrQuasiQuote` path required by the pinned R22 evidence. Genia's
  other reserved keywords/special forms this slice does not implement
  (`import`, `pattern`, `extend`, `use`, `delay`, `unquote`,
  `unquote_splicing`, `some`, `none`, `nil`) are rejected outright rather
  than silently misparsed as ordinary names or calls -- a real bug this
  slice's own preparation
  caught and fixed by running genia-2026's *full* shared spec corpus
  (not just this slice's pinned evidence) against early builds (`extend`/
  `use` are R20 cross-module contribution/selection, which require
  `multi_file_eval` and are hard-rejected rather than given the
  reference parser's own partial-backtrack nuance, to avoid the same
  class of silent-misparse risk with no diagnostic to fall back on).
  Anything else outside this grammar (parens as anything but
  grouping/lambda, general postfix call application on a non-identifier
  expression, guard clauses, leading-/trailing-dot numeric forms,
  malformed exponents, string escapes, ...) is rejected at the
  tokenizer/parser level and reported `unsupported`, never guessed at.
- `src/core_ir.hpp`, `src/lowering.hpp` — the portable Core IR subset
  this slice produces (`IrLiteral` (including the R21 tagged Decimal
  `{kind: "decimal", coefficient, exponent}` payload), `IrVar`,
  `IrUnary`, `IrBinary`, `IrExprStmt`, `IrList`, `IrMap`, `IrAssign`,
  `IrCall`, `IrLambda`, `IrFuncDef`, `IrOpenFuncDef`,
  `IrCase`/`IrCaseClause`, `IrPipeline`, `IrSpread`) and real AST -> IR
  lowering, matching genia-2026's
  `docs/architecture/core-ir-portability.md` wire shapes exactly
  (`src/ir_projection.hpp`, verified directly against
  `hosts/python/ir_normalize.py`). `IrOpenFuncDef` reuses `IrCaseClause`/
  `IrPatTuple`/`IrPatLiteral` verbatim (no new pattern or dispatch
  representation -- local open-function dispatch against one
  participating unit is identical to the existing case-dispatch
  mechanism, per `docs/design/r20-open-functions-syntax-ir-design.md`
  section 5).
- `src/bignum.hpp` — the in-house arbitrary-precision Integer kernel
  (sign + base-2^32 limbs) per the R24 dependency/toolchain policy: no
  third-party bignum library.
- `src/value.hpp`, `src/environment.hpp` — the runtime value
  representation (exact Integer, exact Decimal (E24-7: coefficient
  `bignum::Integer` + `int64_t` exponent, R22 section 2), exact
  Rational (E24-7 increment 2: `bignum::Integer` numerator/denominator,
  R22 section 3), Boolean, String, Bytes, List, the native in-house
  insertion-ordered
  `OrderedMap`, Outcome (`err(...)` plus the narrow JSON-success and
  mixed-domain `none(...)` results required by E24-7; general Option behavior
  remains unimplemented), Closure, and an opaque placeholder for `print`) and
  the parent-chained lexical environment lambda/function calls evaluate
  their bodies in.
- `src/rational.hpp` — R22 section 3 exact Rational construction/
  canonicalization (`construct_rational`): reduces by the positive gcd
  (`src/bignum.hpp`'s new `Integer::gcd`, the ordinary Euclidean
  algorithm), keeps the denominator positive with sign carried by the
  numerator, and collapses a reduced denominator of 1 to Integer.
- `src/arithmetic.hpp` (E24-7 increment 3) — R22 sections 6-8 exact-family
  `+`/`-`/`*`/`/`/`%`: Decimal-only `+`/`-`/`*` align exponents and
  canonicalize (never collapsing to Integer); any-Rational-operand
  `+`/`-`/`*` go through general numerator/denominator fraction algebra
  reduced via `rational::construct_rational`; `/` reduces the operands'
  cross-multiplied fraction and then picks Integer/Decimal/Rational per
  section 7's table, checking base-10 termination
  (`denominator_terminates_in_base10`) only where the contract requires
  it; `%` computes the exact floor quotient via
  `bignum::Integer::floor_remainder` and reduces `left - q*right` as a
  fraction, promoted per section 6 (not section 7) -- Decimal-only `%`
  is always guaranteed to terminate in base 10 (its denominator is
  always a divisor of a power of 10), so no termination check is needed
  there.
- `src/equality.hpp` — R18 structural/legal-key equality: one internal
  dispatch over Genia semantic kinds, kind-tagged map-key encoding so
  distinct kinds never collide (booleans vs. numbers vs. strings), plus
  R22 section 10.1's "Exact family" numeric-equality/ordering bridge
  (Integer/Decimal/Rational compare by mathematical value in every
  pairing for `==`/`!=` via `exact_family_equal`, and for `< <= > >=`
  via `exact_family_compare`, E24-7 increment 4) -- each value converts
  to an exact numerator/denominator pair, compared via
  cross-multiplication, never the lossy integer-to-host-float cast the
  contract forbids (ordering's cross-multiplication is sign-safe
  because every exact-family kind's converted denominator is always
  strictly positive). No fallback to host container/language equality.
- `src/pattern_match.hpp` — pattern matching for lambda parameters and
  local case dispatch, mirroring genia-2026's real
  `src/genia/pattern_match.py` `match_pattern`/`match_pattern_atom`
  exactly for the pattern kinds this slice implements, including the
  "duplicate binding must agree" conflict rule (`([x, x]) -> true`
  against unequal values does not match).
- `src/native_functions.hpp` — the native `float64`/`exact`/`rational`/`map_new`/
  `map_get`/`map_put`/`map_has?`/`map_remove`/`map_count`/`map_items`/
  `utf8_encode`/`err`/`_sum`/`_seq_type_error` callables. Most are each
  a trivial, argument-pass-through-only wrapper in genia-2026's real
  prelude, or a native primitive there already; `rational(numerator,
  denominator)` requires both arguments to be Integers and delegates
  its actual construction to `src/rational.hpp`.
- `src/global_env.hpp`, `src/evaluator.hpp`, `src/genia2026_known_globals.hpp`
  — `evaluator.hpp` is the Core IR evaluator: closures, named-function
  calls (ordinary and case-dispatch bodies, including recursion),
  pipelines, map/list construction (with `IrSpread` splicing), and the
  one deterministic `Error: Undefined name: <name>` diagnostic for a
  name genuinely absent from genia-2026's real global namespace (see
  `genia2026_known_globals.hpp`'s header comment for why that
  distinction matters: a reference to a real-but-unimplemented Genia
  global like `collect` or `sheet` must stay `unsupported`, never be
  misreported as "undefined"). `global_env.hpp` installs `print` plus a
  small, genuinely Genia-source-level prelude (`sum`, `map`, `map_acc`)
  through the real parse -> lower -> eval pipeline -- never a native C++
  reimplementation of `map`'s dispatch/recursion, per
  `docs/design/r24/native-primitive-inventory.md`'s "list/map prelude
  helpers ... interpreted from the shared Genia prelude source" rule.
- `src/render.hpp` — canonical Integer/Decimal/Rational/Boolean/String/
  List/Map/Outcome display rendering for command/file mode's
  auto-display result; Decimal rendering (`render_decimal`) implements
  R23 section 2.2's fixed/scientific notation rule verbatim, verified
  directly against `src/genia/numeric_runtime.py`'s
  `_canonical_decimal_text`; Rational rendering is R23 section 2.3's
  `<numerator>/<denominator>` atom (denominator always positive after
  canonicalization, so no separate sign handling is needed).
- `src/format.hpp` — the bounded R23 numeric field-format engine. It applies
  alignment to canonical rendered atoms, gates zero-padding/grouping to plain
  numeral shapes, and rounds exact numerator/denominator pairs with arbitrary-
  precision decimal half-up arithmetic. Float64 reuses the exact IEEE-754
  dyadic decoder from comparison/map-key work rather than rounding its shortest
  display spelling. General format composition and first-class `Format(...)`
  values remain unsupported.
- `src/ast_projection.hpp`, `src/ir_projection.hpp`, `src/engine.hpp` —
  wire projections for the `parse`/`lower` operations and the
  parse -> lower -> eval pipeline `eval`/`cli` share; both projections
  return `std::optional` and fail the whole projection (never a
  fabricated JSON `null`) for anything they cannot honestly represent.
- `tests/test_bignum.cpp`, `tests/test_engine.cpp`, `tests/test_protocol.cpp`,
  `tests/test_diagnostic_boundary.cpp` — Catch2 unit tests (internal
  genia-cpp tests, not shared conformance evidence). The last is E24-5's
  (`m0smith/genia-2026#959`) adversarial audit: malformed/resource-limit-
  triggering input at the parser/evaluator/adapter-boundary layers,
  asserting no forbidden text (pre-flight section 5: STL exception
  wording, compiler/runtime-specific type names, OS/library error
  strings, filesystem/library implementation details, demangled C++
  symbols) and, more importantly, no crash. It caught two real crashes
  this ticket fixed: a C++ stack overflow (undefined behavior,
  uncatchable by any try/catch) from a few thousand levels of either
  Genia-level recursive function calls or parser nesting (parenthesized
  grouping, list/map literals/patterns) reliably segfaulted the adapter
  before this slice; `src/evaluator.hpp`'s `kMaxCallDepth` and
  `src/parser.hpp`'s `kMaxNestingDepth` now bound both well below the
  measured crash threshold, converting the crash into an honest
  `unsupported` with no change to any normal-sized program's behavior.
  E24-5 adds no new Genia semantics or capabilities.
- `genia-adapter` (the built binary) declares `parser`, `ast_lowering`,
  `cli_command_mode`, `cli_file_mode`, `open_functions`, and `refs` `supported`,
  and declares `core_ir_eval`, `prelude_autoload`, and `shared_spec_runner`
  `partial`. Every other `spec/manifest.json` capability is `unsupported`.
  `prelude_autoload` is partial because this floor installs only the bounded
  source-level prelude needed by its evidence; `shared_spec_runner` is partial
  for the same reason the Python reference adapter uses that status. Neither
  declaration makes additional cases applicable. `open_functions` is `supported`
  rather than `partial` deliberately: `tools/spec_runner/capabilities.py`'s
  requires-gate only attempts a case whose `requires` list names
  `open_functions` when it is declared exactly `supported` (`partial`
  grants zero evidence credit, by that module's own design), and this
  declaration does not claim cross-module `extend`/`use`, the R20
  diagnostic family (no-matching-case/duplicate-clause/varargs-ambiguity),
  or bare varargs patterns are implemented -- those remain genuinely
  `unsupported` per case, or are gated out entirely by every
  cross-module case's separate `multi_file_eval` requirement (see
  `m0smith/genia-2026#973`/`#974`). Running the full shared spec corpus
  against it: `total=755 passed=141 failed=0 unsupported=614
  protocol_error=0 crash=0 timeout=0 invalid=0` — the 7 pinned E24-4
  cases (`outcome_values`, `lambda_function_call`,
  `pattern_case_dispatch`, `pipeline_composition`,
  `deterministic_runtime_error_behavior`), the 2 pinned E24-6
  `open_functions_r20` cases (`r20-gcd-grouped-clause-equivalent`,
  `r20-gcd-repeated-clauses`), E24-7 increment 1's R21 Decimal-literal
  `parse`/`ir` cases (classification, equivalent-spelling
  normalization, the tagged Decimal payload, unary-negative lowering),
  increment 2's `r22-rational-construction.yaml`/
  `r22-exact-family-equality.yaml`, and increment 3's
  `r22-exact-arithmetic-promotion-lattice.yaml`/
  `r22-exact-division-required-proofs.yaml`/
  `r22-exact-floor-remainder.yaml` all pass, plus the E24-2/E24-3
  pinned cases, the two `spec/eval/r18-*.yaml` cases the Integer/
  Decimal equality-bridge fix restored to passing, and further
  incidental cases this slice's honest, evidence-matched
  grammar/lowering/evaluation also happens to satisfy. Increment 5 adds
  `r22-float64-exact-round-trip.yaml` and
  `r22-float64-magnitude-overflow-rejected.yaml`; increment 6 adds
  `r22-float64-arithmetic.yaml` and both Float64 zero-divisor error cases;
  increment 7 adds `r22-exact-family-and-float64-ordering.yaml`,
  `r22-numeric-map-key-cross-kind.yaml`, and the now-reachable
  `r18-map-equal-keys-share-every-operation.yaml`; increment 8 adds the
  R23 numeric field-format cases and older field-spec cases using the
  same implemented surface; increment 9 adds all six `r23-json-*` cases;
  increment 10 adds the two remaining required R22 eval cases for normalized
  mixed-domain rejection and cross-surface numeric quoting/pattern matching.
- String storage/rendering is byte-transparent (copies UTF-8 bytes
  through unexamined), which correctly handles literal storage,
  equality, and display for any well-formed UTF-8 input, but is not yet
  genuine codepoint-aware iteration (no codepoint counting/indexing) --
  see `docs/design/r24/native-primitive-inventory.md`'s "UTF-8 decode/
  code-point iteration" primitive; that becomes necessary once a
  string-indexing/length function is in scope.
- General `some`/`none` Option behavior beyond increment 9's narrow JSON-success
  path and increment 10's mixed-domain `none("type-error", context)` result,
  general diagnostic normalization (beyond the one undefined-name
  case), and general postfix call application (calling the result of a
  call or a parenthesized expression, e.g. immediately-invoked lambdas)
  remain entirely unimplemented. Decimal and Rational are only *partly*
  implemented: source literals/construction, negation, canonical
  display, and the full R22 section 6-8, 10.1 exact-family
  arithmetic/division/floor-remainder/equality/ordering rules
  (`+ - * / % == != < <= > >=`) all work end to end across
  Integer/Decimal/Rational, and comparison with Float64 now follows section
  10.2's exact represented-value bridge. Numeric field-format-spec integration
  is implemented; first-class `Format(...)` values and compatibility/general JSON
  behavior beyond the six shared strict-numeric cases remain unimplemented. R20 open
  functions are only
  *partly* implemented: local (single-module) grouped/repeated clause
  dispatch works end to end, but cross-module `extend`/`use`
  contribution and selection, the R20 diagnostic family
  (`open-function-no-matching-case`/`open-function-duplicate-clause`/
  `open-function-varargs-ambiguity`), and bare/top-level varargs rest
  patterns (`open f(x, ..rest) = ...`) all remain unimplemented — see
  later slices'/`genia-2026`'s
  [`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md).

## Explicitly deferred after R24

- R25 E25-1 Ref and E25-2 Cell are implemented on this branch. Local Process
  (E25-3) remains unsupported. Actor is excluded from R25 and belongs to R38.
- R26: REPL and broader data bridges.
- R27: Flow, pipe mode, HTTP serving, and outbound HTTP.
- Cross-module open-function contribution/selection and wider parser/evaluator
  behavior outside the R24 floor, including general Option and compatibility JSON.
- Configuration/secrets, resource I/O, external execution, AI/retrieval providers,
  host interop, debugger/shell, browser runtime, and help/documentation parity.

These are expected `unsupported` results, not hidden failures or Python-parity claims.

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
# total=755 passed=141 failed=0 unsupported=614 protocol_error=0 crash=0 timeout=0 invalid=0
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
