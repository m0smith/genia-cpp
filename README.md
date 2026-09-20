# genia-cpp

**Status: E24-4 vertical slice complete. `genia-adapter` implements
integer/string/boolean/list/map literals, bare-name references,
assignment, `+ - * / ==` binary expressions, lambdas/closures, named
functions (ordinary and local case/pattern-dispatch bodies), pipelines
(`|>`), the `err(...)` Outcome constructor and its rendering, one
deterministic undefined-name runtime error, calls to the native
`map_*`/`utf8_encode`/`err`/`sum` functions, and `-c`/file-mode CLI --
end to end (source -> parser -> portable Core IR -> evaluator ->
normalized adapter result). Every other Genia behavior remains honestly
`unsupported`.**

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
E24-1 through E24-4 are complete; E24-5 through E24-8 remain.

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

## Pinned contract

| | |
|---|---|
| `genia-2026` contract revision | [`df309a9c1610b9989dd730cc41b44ad350287637`](https://github.com/m0smith/genia-2026/commit/df309a9c1610b9989dd730cc41b44ad350287637) |
| E16-1 adapter-protocol version | `1` |
| Represents | `genia-2026` `main` after merging `#969` (E24-3 bootstrap evidence fix), found necessary while preparing E24-3. This is the exact revision `src/protocol.hpp`'s `kContractRevision` declares and `genia-adapter`'s `capabilities` response reports. |

This is a **pinned-conformance declaration** in the sense E16-4 defines it
(`genia-2026`'s `tools/spec_runner/revision.py`): this repository's
implementation was built against that exact `genia-2026` commit. Update
this table deliberately as this repository advances — never silently, and
never by copying semantic behavior instead of the declared revision
identity.

## What exists here

E24-4 (`m0smith/genia-2026#958`) widens E24-3's vertical slice to cover
Outcome values, lambdas/closures, local case/pattern dispatch,
pipelines, and one deterministic runtime-error diagnostic:

- `src/protocol.hpp` — the E16-1 wire-envelope helpers, plus the
  per-capability status overrides (`parser`/`ast_lowering`/
  `cli_command_mode`/`cli_file_mode` `supported`, `core_ir_eval`
  `partial`) this slice earned with real evidence.
- `src/adapter.hpp` — request classification/dispatch for
  `capabilities`/`parse`/`lower`/`eval`/`cli` (including both `-c`
  command mode and bare-file-path file mode), routing the latter four
  through `src/engine.hpp`.
- `src/parser.hpp`, `src/ast.hpp`, `src/pattern.hpp` — a tokenizer and
  recursive-descent parser for exactly this slice's grammar:
  integer/string/boolean/list/map literals, assignment, named-function
  definitions (`name(params) = body`, ordinary or local
  case/pattern-dispatch), lambdas (`(params) -> body`, including
  list/map-destructuring parameters), pipelines (`|>`), general
  parenthesized grouping, function calls, the bare-name references
  every other identifier now resolves to (whether a name is actually
  *bound* is a runtime concern -- see `src/evaluator.hpp` -- not a
  parse-time restriction, matching genia-2026's real grammar), and
  `+ - * / ==` binary expressions with standard precedence. `pattern.hpp`
  holds the one pattern-shape struct (Bind/Wildcard/Rest/List/Map/Tuple)
  shared by the parser and evaluator, matching
  `docs/architecture/core-ir-portability.md`'s named pattern families.
  Genia's own reserved keywords/special forms this slice does not
  implement (`import`, `pattern`, `quote`, `delay`, `quasiquote`,
  `unquote`, `unquote_splicing`, `some`, `none`, `nil`) are rejected
  outright rather than silently misparsed as ordinary names or calls --
  a real bug this slice's own preparation caught and fixed by running
  genia-2026's *full* shared spec corpus (not just this slice's pinned
  evidence) against early builds. Anything else outside this grammar
  (parens as anything but grouping/lambda, unary minus, general postfix
  call application on a non-identifier expression, guard clauses,
  decimal/exponent literals, string escapes, ...) is rejected at the
  tokenizer/parser level and reported `unsupported`, never guessed at.
- `src/core_ir.hpp`, `src/lowering.hpp` — the portable Core IR subset
  this slice produces (`IrLiteral`, `IrVar`, `IrBinary`, `IrExprStmt`,
  `IrList`, `IrMap`, `IrAssign`, `IrCall`, `IrLambda`, `IrFuncDef`,
  `IrCase`/`IrCaseClause`, `IrPipeline`, `IrSpread`) and real AST -> IR
  lowering, matching genia-2026's
  `docs/architecture/core-ir-portability.md` wire shapes exactly
  (`src/ir_projection.hpp`; not yet exercised by pinned `lower`-category
  evidence, but built honestly against the documented contract rather
  than deferred).
- `src/bignum.hpp` — the in-house arbitrary-precision Integer kernel
  (sign + base-2^32 limbs) per the R24 dependency/toolchain policy: no
  third-party bignum library.
- `src/value.hpp`, `src/environment.hpp` — the runtime value
  representation (exact Integer, Boolean, String, Bytes, List, the
  native in-house insertion-ordered `OrderedMap`, Outcome (`err(...)`
  only -- `some`/`none` remain unimplemented), Closure, and an opaque
  placeholder for `print`) and the parent-chained lexical environment
  lambda/function calls evaluate their bodies in.
- `src/equality.hpp` — R18 structural/legal-key equality: one internal
  dispatch over Genia semantic kinds, kind-tagged map-key encoding so
  distinct kinds never collide (booleans vs. numbers vs. strings), no
  fallback to host container/language equality.
- `src/pattern_match.hpp` — pattern matching for lambda parameters and
  local case dispatch, mirroring genia-2026's real
  `src/genia/pattern_match.py` `match_pattern`/`match_pattern_atom`
  exactly for the pattern kinds this slice implements, including the
  "duplicate binding must agree" conflict rule (`([x, x]) -> true`
  against unequal values does not match).
- `src/native_functions.hpp` — the native `map_new`/`map_get`/
  `map_put`/`map_has?`/`map_remove`/`map_count`/`map_items`/
  `utf8_encode`/`err`/`_sum`/`_seq_type_error` callables (each a
  trivial, argument-pass-through-only wrapper in genia-2026's real
  prelude, or itself a native primitive there).
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
- `src/render.hpp` — canonical Integer/Boolean/String/List/Map/Outcome
  display rendering for command/file mode's auto-display result.
- `src/ast_projection.hpp`, `src/ir_projection.hpp`, `src/engine.hpp` —
  wire projections for the `parse`/`lower` operations and the
  parse -> lower -> eval pipeline `eval`/`cli` share; both projections
  return `std::optional` and fail the whole projection (never a
  fabricated JSON `null`) for anything they cannot honestly represent.
- `tests/test_bignum.cpp`, `tests/test_engine.cpp`, `tests/test_protocol.cpp`
  — Catch2 unit tests (internal genia-cpp tests, not shared conformance
  evidence).
- `genia-adapter` (the built binary) declares `parser`, `ast_lowering`,
  `cli_command_mode`, and `cli_file_mode` `supported`, `core_ir_eval`
  `partial`, and every other `spec/manifest.json` capability
  `unsupported`. Running the full shared spec corpus against it:
  `total=744 passed=64 failed=0 unsupported=680 protocol_error=0
  crash=0 timeout=0 invalid=0` — the 7 pinned E24-4 cases
  (`outcome_values`, `lambda_function_call`, `pattern_case_dispatch`,
  `pipeline_composition`, `deterministic_runtime_error_behavior`) all
  pass, plus the E24-2/E24-3 pinned cases and further incidental cases
  this slice's honest, evidence-matched grammar/lowering/evaluation also
  happens to satisfy.
- String storage/rendering is byte-transparent (copies UTF-8 bytes
  through unexamined), which correctly handles literal storage,
  equality, and display for any well-formed UTF-8 input, but is not yet
  genuine codepoint-aware iteration (no codepoint counting/indexing) --
  see `docs/design/r24/native-primitive-inventory.md`'s "UTF-8 decode/
  code-point iteration" primitive; that becomes necessary once a
  string-indexing/length function is in scope.
- `some`/`none` Option values, Decimal/Rational/Float64, open functions,
  general diagnostic normalization (beyond the one undefined-name case),
  and general postfix call application (calling the result of a call or
  a parenthesized expression, e.g. immediately-invoked lambdas) remain
  entirely unimplemented — later slices'/`genia-2026`'s
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
# total=744 passed=64 failed=0 unsupported=680 protocol_error=0 crash=0 timeout=0 invalid=0
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
