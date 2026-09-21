# genia-cpp

**Status: E24-6 complete, E24-7 in progress (increments 1-2 landed).
`genia-adapter` implements integer/string/boolean/list/map literals,
Decimal source literals (`1.25`, `1e3`, ...; negation and canonical
display only -- no arithmetic beyond negation yet), the exact Rational
runtime value and `rational(numerator, denominator)` construction
(gcd-reduced, canonical `<numerator>/<denominator>` display -- no
arithmetic yet either), unary minus, bare-name references, assignment,
`+ - * / == != %` binary expressions, lambdas/closures, named functions
(ordinary and local case/pattern-dispatch bodies), local R20 open
functions (grouped and repeated top-level clause spellings, single
module only), pipelines (`|>`), the `err(...)` Outcome constructor and
its rendering, the R22 exact-family (Integer/Decimal/Rational)
numeric-equality bridge, one deterministic undefined-name runtime
error, calls to the native `map_*`/`utf8_encode`/`err`/`sum` functions,
and `-c`/file-mode CLI -- end to end (source -> parser -> portable Core
IR -> evaluator -> normalized adapter result), hardened against a C++
stack-overflow crash from adversarially deep recursion/nesting (E24-5).
Every other Genia behavior remains honestly `unsupported`.**

This is the planned production C++ host for [Genia](https://github.com/m0smith/genia-2026).
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
E24-1 through E24-6 are complete; E24-7 is in progress (increments 1-2
of several); E24-8 remains.

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
| `genia-2026` contract revision | [`eb171afc434b3b9110007b8be76f6b0eff850311`](https://github.com/m0smith/genia-2026/commit/eb171afc434b3b9110007b8be76f6b0eff850311) |
| E16-1 adapter-protocol version | `1` |
| Represents | `genia-2026` `main` after merging `#974` (the `open_functions` `supported`-not-`partial` guidance correction), found necessary while preparing E24-6. This is the exact revision `src/protocol.hpp`'s `kContractRevision` declares and `genia-adapter`'s `capabilities` response reports. |

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
never rounding through a host binary float. Rational arithmetic,
comparison operators (`< <= > >=`, absent from this slice's grammar
entirely), Float64, the format-spec engine, and the JSON boundary all
remain further E24-7 increments:

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
  `+ - * / == != %` binary expressions with standard precedence (`%` is
  exact floor-remainder, Python-style, sign follows the divisor --
  verified directly against `src/genia/numeric_runtime.py`'s
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
  pattern families. Genia's own reserved keywords/special forms this
  slice does not implement (`import`, `pattern`, `extend`, `use`,
  `quote`, `delay`, `quasiquote`, `unquote`, `unquote_splicing`, `some`,
  `none`, `nil`) are rejected outright rather than silently misparsed as
  ordinary names or calls -- a real bug this slice's own preparation
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
  `bignum::Integer` + `int64_t` exponent, R22 section 2 -- literal
  construction and negation only, no arithmetic beyond that yet), exact
  Rational (E24-7 increment 2: `bignum::Integer` numerator/denominator,
  R22 section 3 -- construction and display only, no arithmetic yet),
  Boolean, String, Bytes, List, the native in-house insertion-ordered
  `OrderedMap`, Outcome (`err(...)` only -- `some`/`none` remain
  unimplemented), Closure, and an opaque placeholder for `print`) and
  the parent-chained lexical environment lambda/function calls evaluate
  their bodies in.
- `src/rational.hpp` — R22 section 3 exact Rational construction/
  canonicalization (`construct_rational`): reduces by the positive gcd
  (`src/bignum.hpp`'s new `Integer::gcd`, the ordinary Euclidean
  algorithm), keeps the denominator positive with sign carried by the
  numerator, and collapses a reduced denominator of 1 to Integer.
- `src/equality.hpp` — R18 structural/legal-key equality: one internal
  dispatch over Genia semantic kinds, kind-tagged map-key encoding so
  distinct kinds never collide (booleans vs. numbers vs. strings), plus
  R22 section 10.1's "Exact family" numeric-equality bridge
  (Integer/Decimal/Rational compare by mathematical value in every
  pairing, `exact_family_equal`) -- each value converts to an exact
  numerator/denominator pair, compared via cross-multiplication, never
  the lossy integer-to-host-float cast the contract forbids. No
  fallback to host container/language equality.
- `src/pattern_match.hpp` — pattern matching for lambda parameters and
  local case dispatch, mirroring genia-2026's real
  `src/genia/pattern_match.py` `match_pattern`/`match_pattern_atom`
  exactly for the pattern kinds this slice implements, including the
  "duplicate binding must agree" conflict rule (`([x, x]) -> true`
  against unequal values does not match).
- `src/native_functions.hpp` — the native `rational`/`map_new`/
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
  `cli_command_mode`, `cli_file_mode`, and `open_functions` `supported`,
  `core_ir_eval` `partial`, and every other `spec/manifest.json`
  capability `unsupported`. `open_functions` is declared `supported`
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
  against it: `total=755 passed=89 failed=0 unsupported=666
  protocol_error=0 crash=0 timeout=0 invalid=0` — the 7 pinned E24-4
  cases (`outcome_values`, `lambda_function_call`,
  `pattern_case_dispatch`, `pipeline_composition`,
  `deterministic_runtime_error_behavior`), the 2 pinned E24-6
  `open_functions_r20` cases (`r20-gcd-grouped-clause-equivalent`,
  `r20-gcd-repeated-clauses`), E24-7 increment 1's R21 Decimal-literal
  `parse`/`ir` cases (classification, equivalent-spelling
  normalization, the tagged Decimal payload, unary-negative lowering),
  and increment 2's `r22-rational-construction.yaml`/
  `r22-exact-family-equality.yaml` all pass, plus the E24-2/E24-3
  pinned cases, the two `spec/eval/r18-*.yaml` cases the Integer/
  Decimal equality-bridge fix restored to passing, and further
  incidental cases this slice's honest, evidence-matched
  grammar/lowering/evaluation also happens to satisfy.
- String storage/rendering is byte-transparent (copies UTF-8 bytes
  through unexamined), which correctly handles literal storage,
  equality, and display for any well-formed UTF-8 input, but is not yet
  genuine codepoint-aware iteration (no codepoint counting/indexing) --
  see `docs/design/r24/native-primitive-inventory.md`'s "UTF-8 decode/
  code-point iteration" primitive; that becomes necessary once a
  string-indexing/length function is in scope.
- `some`/`none` Option values, Float64, ordered comparison operators
  (`< <= > >=`, absent from this slice's grammar for every numeric
  kind, not just the newer ones), general diagnostic normalization
  (beyond the one undefined-name case), and general postfix call
  application (calling the result of a call or a parenthesized
  expression, e.g. immediately-invoked lambdas) remain entirely
  unimplemented. Decimal and Rational are only *partly* implemented:
  source literals/construction, negation (Decimal only), canonical
  display, and R22 section 10.1's exact-family `==`/`!=` bridge across
  Integer/Decimal/Rational all work end to end, but arithmetic
  (`+ - * / %`) for anything beyond plain Integer/Integer, the
  `float64(...)`/`exact(...)` conversions, field-format-spec
  integration, and the JSON boundary all remain unimplemented. R20 open
  functions are only
  *partly* implemented: local (single-module) grouped/repeated clause
  dispatch works end to end, but cross-module `extend`/`use`
  contribution and selection, the R20 diagnostic family
  (`open-function-no-matching-case`/`open-function-duplicate-clause`/
  `open-function-varargs-ambiguity`), and bare/top-level varargs rest
  patterns (`open f(x, ..rest) = ...`) all remain unimplemented — see
  later slices'/`genia-2026`'s
  [`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md).

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
# total=755 passed=89 failed=0 unsupported=666 protocol_error=0 crash=0 timeout=0 invalid=0
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
