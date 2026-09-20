# genia-cpp

**Status: E24-2 vertical slice complete. `genia-adapter` implements one
deliberately minimal path -- integer literals, bare-name references,
`+ - * /` binary expressions, and `-c` command mode -- end to end
(source -> parser -> portable Core IR -> evaluator -> normalized
adapter result). Every other Genia behavior remains honestly
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
E24-1 (toolchain bootstrap) and E24-2 (this vertical slice) are complete;
E24-3 through E24-8 remain.

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
E24-2's own preparation found and fixed two such gaps upstream before
implementing: `m0smith/genia-2026#963`/`#964` (a bootstrap evidence case
required out-of-scope pattern dispatch) and `#965`/`#966` (the generic
external-host `cli`-category path never stripped trailing newlines,
unlike the in-process Python-host path every `spec/cli/*.yaml` case's
expected output assumes).

## Pinned contract

| | |
|---|---|
| `genia-2026` contract revision | [`dfa9aa5c32eec7e7b33f9a221dbad7f1c5bcd548`](https://github.com/m0smith/genia-2026/commit/dfa9aa5c32eec7e7b33f9a221dbad7f1c5bcd548) |
| E16-1 adapter-protocol version | `1` |
| Represents | `genia-2026` `main` after merging `#964` (bootstrap-cases.json evidence fix) and `#966` (host_executor.py cli-category newline-stripping fix), both found necessary while preparing E24-2. This is the exact revision `src/protocol.hpp`'s `kContractRevision` declares and `genia-adapter`'s `capabilities` response reports. |

This is a **pinned-conformance declaration** in the sense E16-4 defines it
(`genia-2026`'s `tools/spec_runner/revision.py`): this repository's
implementation was built against that exact `genia-2026` commit. Update
this table deliberately as this repository advances — never silently, and
never by copying semantic behavior instead of the declared revision
identity.

## What exists here

E24-2 (`m0smith/genia-2026#956`) adds the first real Genia semantics on
top of E24-1's honest bootstrap adapter:

- `src/protocol.hpp` — the E16-1 wire-envelope helpers, plus the
  per-capability status overrides (`parser`/`ast_lowering`/
  `cli_command_mode` `supported`, `core_ir_eval` `partial`) this slice
  earned with real evidence.
- `src/adapter.hpp` — request classification/dispatch for
  `capabilities`/`parse`/`lower`/`eval`/`cli`, routing the latter four
  through `src/engine.hpp`.
- `src/parser.hpp`, `src/ast.hpp` — a tokenizer and recursive-descent
  parser for exactly this slice's grammar: integer literals, the one
  evidenced bare-name reference (`print`), and `+ - * /` binary
  expressions with standard precedence. Anything outside that grammar
  (parens, unary minus, strings, lists, decimal/exponent literals,
  other identifiers, ...) is rejected at the tokenizer/parser level and
  reported `unsupported`, never guessed at.
- `src/core_ir.hpp`, `src/lowering.hpp` — the portable Core IR subset
  this slice produces (`IrLiteral`, `IrVar`, `IrBinary`, `IrExprStmt`)
  and real AST -> IR lowering, matching genia-2026's
  `docs/architecture/core-ir-portability.md` wire shapes exactly
  (verified against `spec/ir/r21-*.yaml` evidence, not guessed).
- `src/bignum.hpp` — the in-house arbitrary-precision Integer kernel
  (sign + base-2^32 limbs) per the R24 dependency/toolchain policy: no
  third-party bignum library.
- `src/value.hpp`, `src/global_env.hpp`, `src/evaluator.hpp` — the
  runtime value representation (exact Integer, plus an opaque
  placeholder for the one evidenced global name) and Core IR evaluator.
- `src/render.hpp` — canonical Integer display rendering for command
  mode's auto-display result.
- `src/ast_projection.hpp`, `src/ir_projection.hpp`, `src/engine.hpp` —
  wire projections for the `parse`/`lower` operations and the
  parse -> lower -> eval pipeline `eval`/`cli` share.
- `tests/test_bignum.cpp`, `tests/test_engine.cpp`, `tests/test_protocol.cpp`
  — Catch2 unit tests (internal genia-cpp tests, not shared conformance
  evidence).
- `genia-adapter` (the built binary) declares `parser`, `ast_lowering`,
  and `cli_command_mode` `supported`, `core_ir_eval` `partial`, and
  every other `spec/manifest.json` capability `unsupported`. Running the
  full shared spec corpus against it: `total=740 passed=10 failed=0
  unsupported=730 protocol_error=0 crash=0 timeout=0 invalid=0` — the 10
  passes are the 4 cases `docs/design/r24/bootstrap-cases.json` pins for
  E24-2 (`parse-literal-number`, `arithmetic-basic`,
  `integer-arithmetic-large-magnitude-no-overflow`,
  `command_mode_basic`) plus 6 incidental cases this slice's honest,
  evidence-matched grammar/lowering also happens to satisfy:
  `eval/arithmetic-precedence`, `ir/r21-huge-integer-literal-tagged-payload`,
  `ir/r21-integer-literal-tagged-payload`,
  `ir/r21-slash-remains-ordinary-binary`, `ir/slash-operator`, and
  `parse/parse-r21-integer-source-classification`.
- Lists, maps, lambdas, pattern matching, Decimal/Rational/Float64, file
  mode, and open functions remain entirely unimplemented — that starts
  at E24-3 (`genia-2026`'s
  [`docs/strategy/roadmap/e24-issue-sequence.md`](https://github.com/m0smith/genia-2026/blob/main/docs/strategy/roadmap/e24-issue-sequence.md)).

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
# total=740 passed=10 failed=0 unsupported=730 protocol_error=0 crash=0 timeout=0 invalid=0
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
