# Model B Slice A: parser and portable Core IR lowering (genia-cpp#50)

This host parses and lowers the approved Model B Slice A syntax. It executes
none of it. Authority: genia-2026 `GENIA_STATE.md` ("Model B declarations") and
`docs/architecture/core-ir-portability.md` ("Model B declaration carriers").
Pinned contract revision: `d005c89f852ee2d27682dade272eabfd158adf12`.

## Implemented (parse/lower only)

- `=:` token, matched after `==` and before `=`. `=:` inside strings or comments is not a token.
- `name =: expr` → `IrImmutableBind` (top level and inside blocks).
- Consecutive same-name `f(...) =: body` clauses → one ordered `IrClosedFuncDef`
  of `IrFuncDef`/`IrPatternFuncDef` children. Comments never end a group; any other
  statement does.
- Pattern/guard headers (any header that is not plain identifiers with an optional
  final named rest, or that has `? guard`) → `IrPatternFuncDef`. This includes a
  full tuple pattern, an optional guard, and a post-`=` string that is a body
  rather than a docstring.
- Legacy `IrFuncDef` keeps its wire form. New: `rest_param` (`f(..xs) = xs`) and the
  historical `name(x) { ... }` block form.
- `@name value` prefix annotations, only on an immutable binding or a closed-group
  child. Any other annotated target is still unsupported.
- `IrPatErr` (an absent context is `null`) and `IrPatNamed` are serialized in every
  position, including legacy lambda and case patterns. `IrPatSome.context` is
  serialized only inside `IrPatternFuncDef` headers (the documented asymmetry).
- Malformed-carrier validation in `src/ir_projection.hpp`. An invalid carrier is
  refused (unsupported) before any child is read, never serialized or partially
  emitted:
  - Every pattern's child count is checked: `Err`/`Some` need exactly two children,
    `Named` exactly one, and leaf patterns none.
  - Every required child (bodies, initializers, operands) must be present.
  - A grouped function needs parallel pattern/result arrays and, when it has guards,
    one guard slot per clause.
  - A closed group is nonempty and contains only same-name, well-formed
    `IrFuncDef`/`IrPatternFuncDef` children. A child body contains no nested
    function declaration of any kind: not in a block, lambda, grouped-arm result or
    guard. A local `IrImmutableBind` is permitted.
  - A pattern declaration needs one tuple header and has no params, rest, docstring
    or grouped body.
- Bare `true`/`false` in any pattern position are Boolean literal patterns
  (`IrPatLiteral` with a JSON boolean, matched by R18 equality), so `f(true) = …` is
  an `IrPatternFuncDef`. Identifiers such as `truth` stay parameter binds.
- Contract SyntaxErrors are reported by the `parse` operation only:
  - `Assignment target must be a simple name`
  - `Function declaration target must be a simple name`
  - `pattern/guard function headers cannot use ->`
  - `pattern/guard function header cannot have a grouped case body`
  - `Unexpected end of input after '=:'`
  - `mixed-function-declaration-operators`

  `lower`, `eval` and `cli` report a source with a syntax error as unsupported.

## Explicitly unsupported (unchanged capabilities)

`immutable_bindings`, `default_open_functions`, `closed_functions` and
`multi_file_eval` are all declared `unsupported`. The evaluator refuses every
`IrImmutableBind`, `IrPatternFuncDef` and `IrClosedFuncDef`, so none of them ever
runs as `=`. It also refuses annotated or varargs functions, named-pattern
references, and one-argument `err(r)`/`some(x)` patterns. These are refused at
definition time and again at match time. Bare `none`/`nil` patterns stay
unsupported and are never identifier binds. The parse-operation AST projection
claims no shape for the new carriers.

Two exposed baseline gaps were fixed, each with its own RED test:
- Bare `none`/`nil` were lowered as `IrPatBind`.
- Closures were never equal to themselves (R18 identity: `f == f` was `false`).

## PR #61 review corrections

The independent review of `a4673b7` raised five blocking findings. Each one got a
RED test first, in commit `4100936`. The RED output is saved in
[`docs/evidence/model-b-review-red.md`](evidence/model-b-review-red.md).

| # | Finding | RED at `a4673b7` | GREEN |
|---|---|---|---|
| 1 | `Err`/`Some`/`Named` child counts are not checked | SIGSEGV | `73e02ab` |
| 2 | Closed `FuncDef` children: missing body, mismatched arrays | SIGSEGV | `73e02ab` |
| 3 | `ImmutableBind` without an initializer | SIGSEGV | `73e02ab` |
| 4 | Nested declarations in closed child bodies | 6 nested forms serialized | `73e02ab` |
| 5 | `true`/`false` lowered as identifier binds | `IrPatBind("true")`, `params:["false"]` | `7832f9c` |

The adversarial checks run in the fork-isolated `genia-model-b-malformed-ir-check`.
The source-path and Boolean checks run in `genia-model-b-slice-a-check`.

## Evidence

Commits on `issue-50-model-b-slice-a-parser`:
- RED `da83d0b`, then RED `71d6994`
- GREEN `150680f` (parser/lowering), then GREEN `35095be` (identity)
- Pin/docs `7cf9356`, then the documentation-compliance/test-harness commit

At baseline `fef41e5`, 36 of the 41 parse/IR cases failed in the RED suite
(Catch2 `tests/test_model_b_slice_a.cpp`, commits `da83d0b`/`71d6994`). The same
assertions now run as the `genia-model-b-slice-a-check` CTest target
(`tests/check_model_b_slice_a.cpp`). That target also adds direct-IR
malformed-carrier checks, which were written alongside GREEN rather than before
it. Catch2 macros cannot be parsed by the code-documentation checker, and its
baseline may not grow.

Full shared corpus at `d005c89`:

| | total | pass | fail | unsupported | other |
|---|---|---|---|---|---|
| baseline `fef41e5` | 1146 | 295 | 0 | 851 | 0 |
| final | 1146 | 335 | 0 | 811 | 0 |

No case went from PASS to non-PASS. 40 cases newly pass:
- the 36 `model-b-a-*` gaps
- `block-body-function`
- `funcdef-varargs`
- `parse-named-pattern-use-in-case-arm`
- `model-b-c-open-lambda-factory-distinct`

### The 43 `model-b-a-*` cases

| # | Case | Cat | Obligation | Baseline (fef41e5) | Final |
|---|---|---|---|---|---|
| 1 | `model-b-a-recursion-in-list` | eval | eval (no Model B syntax; needs `nil`) | UNSUPPORTED | UNSUPPORTED |
| 2 | `model-b-a-recursion-in-map` | eval | eval (no Model B syntax; needs `nil`) | UNSUPPORTED | UNSUPPORTED |
| 3 | `model-b-a-annotated-immutable` | ir | ast_lowering | UNSUPPORTED | PASS |
| 4 | `model-b-a-calls` | ir | ast_lowering | PASS | PASS |
| 5 | `model-b-a-closed-grouped` | ir | ast_lowering | UNSUPPORTED | PASS |
| 6 | `model-b-a-closed-run` | ir | ast_lowering | UNSUPPORTED | PASS |
| 7 | `model-b-a-closed` | ir | ast_lowering | UNSUPPORTED | PASS |
| 8 | `model-b-a-guard-identifier` | ir | ast_lowering | UNSUPPORTED | PASS |
| 9 | `model-b-a-guard-pattern` | ir | ast_lowering | UNSUPPORTED | PASS |
| 10 | `model-b-a-immutable` | ir | ast_lowering | UNSUPPORTED | PASS |
| 11 | `model-b-a-legacy-dotted-assignment` | ir | ast_lowering | PASS | PASS |
| 12 | `model-b-a-legacy-dotted-function-arrow` | ir | ast_lowering | PASS | PASS |
| 13 | `model-b-a-legacy-dotted-function-block` | ir | ast_lowering | UNSUPPORTED | PASS |
| 14 | `model-b-a-legacy-dotted-function` | ir | ast_lowering | PASS | PASS |
| 15 | `model-b-a-legacy-lambda-err` | ir | ast_lowering | UNSUPPORTED | PASS |
| 16 | `model-b-a-legacy-lambda-named` | ir | ast_lowering | UNSUPPORTED | PASS |
| 17 | `model-b-a-legacy-rest` | ir | ast_lowering | UNSUPPORTED | PASS |
| 18 | `model-b-a-legacy-some-context` | ir | ast_lowering | UNSUPPORTED | PASS |
| 19 | `model-b-a-ordinary-independent` | ir | ast_lowering | PASS | PASS |
| 20 | `model-b-a-pattern-err-context` | ir | ast_lowering | UNSUPPORTED | PASS |
| 21 | `model-b-a-pattern-err` | ir | ast_lowering | UNSUPPORTED | PASS |
| 22 | `model-b-a-pattern-list` | ir | ast_lowering | UNSUPPORTED | PASS |
| 23 | `model-b-a-pattern-literal` | ir | ast_lowering | UNSUPPORTED | PASS |
| 24 | `model-b-a-pattern-map` | ir | ast_lowering | UNSUPPORTED | PASS |
| 25 | `model-b-a-pattern-named` | ir | ast_lowering | UNSUPPORTED | PASS |
| 26 | `model-b-a-pattern-some-context` | ir | ast_lowering | UNSUPPORTED | PASS |
| 27 | `model-b-a-pattern-string-body` | ir | ast_lowering | UNSUPPORTED | PASS |
| 28 | `model-b-a-pattern-tuple` | ir | ast_lowering | UNSUPPORTED | PASS |
| 29 | `model-b-a-split` | ir | ast_lowering | UNSUPPORTED | PASS |
| 30 | `model-b-a-dotted-closed-target` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 31 | `model-b-a-dotted-guard-target` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 32 | `model-b-a-dotted-pattern-target` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 33 | `model-b-a-guard-arrow` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 34 | `model-b-a-guard-grouped` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 35 | `model-b-a-index` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 36 | `model-b-a-missing-body` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 37 | `model-b-a-missing-init` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 38 | `model-b-a-mixed-reverse` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 39 | `model-b-a-mixed` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 40 | `model-b-a-nested` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 41 | `model-b-a-pattern-arrow` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 42 | `model-b-a-pattern-grouped` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |
| 43 | `model-b-a-target` | parse | parser (SyntaxError) | UNSUPPORTED | PASS |

The two eval cases contain no Model B syntax. They fail only because of the
pre-existing `nil` literal runtime gap: substituting `0` for `nil`, this host
already prints `[20]` and `{value: 20}`. That gap is outside #50.
