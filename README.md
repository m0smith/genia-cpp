# genia-cpp

**Status: E24-3 vertical slice complete. `genia-adapter` implements
integer/string/boolean/list literals, bare-name references, assignment,
`+ - * / ==` binary expressions, calls to the native `map_*`/`utf8_encode`
functions, and `-c`/file-mode CLI -- end to end (source -> parser ->
portable Core IR -> evaluator -> normalized adapter result). Every other
Genia behavior remains honestly `unsupported`.**

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
E24-1 through E24-3 are complete; E24-4 through E24-8 remain.

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

E24-3 (`m0smith/genia-2026#957`) widens E24-2's vertical slice to cover
R17/R18's portable data-structure and equality contracts, plus the
second CLI entry point:

- `src/protocol.hpp` — the E16-1 wire-envelope helpers, plus the
  per-capability status overrides (`parser`/`ast_lowering`/
  `cli_command_mode`/`cli_file_mode` `supported`, `core_ir_eval`
  `partial`) this slice earned with real evidence.
- `src/adapter.hpp` — request classification/dispatch for
  `capabilities`/`parse`/`lower`/`eval`/`cli` (including both `-c`
  command mode and bare-file-path file mode), routing the latter four
  through `src/engine.hpp`.
- `src/parser.hpp`, `src/ast.hpp` — a tokenizer and recursive-descent
  parser for exactly this slice's grammar: integer/string/boolean
  literals, list literals, assignment, function calls, the evidenced
  bare-name references (`print` plus any name the program itself
  assigns), and `+ - * / ==` binary expressions with standard
  precedence. Anything outside that grammar (parens, unary minus,
  lambdas, pattern matching, decimal/exponent literals, string escapes,
  other identifiers, ...) is rejected at the tokenizer/parser level and
  reported `unsupported`, never guessed at.
- `src/core_ir.hpp`, `src/lowering.hpp` — the portable Core IR subset
  this slice produces (`IrLiteral`, `IrVar`, `IrBinary`, `IrExprStmt`,
  `IrList`, `IrAssign`, `IrCall`) and real AST -> IR lowering, matching
  genia-2026's `docs/architecture/core-ir-portability.md` wire shapes
  exactly (verified against `spec/ir/*.yaml` evidence, not guessed;
  `IrLiteral`'s payload varies by literal kind -- only numeric literals
  get R21's tagged payload, string/bool are plain, per R21 E21-2).
- `src/bignum.hpp` — the in-house arbitrary-precision Integer kernel
  (sign + base-2^32 limbs) per the R24 dependency/toolchain policy: no
  third-party bignum library.
- `src/value.hpp` — the runtime value representation: exact Integer,
  Boolean, String, Bytes (from `utf8_encode`), List, the native
  in-house insertion-ordered `OrderedMap` (vector of pairs + hash
  index, never `std::map`/`std::unordered_map`), and an opaque
  placeholder for the one evidenced global name that isn't yet
  callable.
- `src/equality.hpp` — R18 structural/legal-key equality: one internal
  dispatch over Genia semantic kinds, kind-tagged map-key encoding so
  distinct kinds never collide (booleans vs. numbers vs. strings), no
  fallback to host container/language equality.
- `src/native_functions.hpp` — the native `map_new`/`map_get`/
  `map_put`/`map_has?`/`map_remove`/`map_count`/`map_items`/
  `utf8_encode` callables. The map functions are, in genia-2026's real
  prelude, trivial single-clause pass-throughs to same-named native
  primitives (e.g. `map_put(map, key, value) = _map_put(map, key,
  value)`); this slice implements them natively rather than
  interpreting that prelude source text because it has no user-level
  function *definition* support yet (no pinned evidence needs one) and
  the observable behavior is identical either way. Genuine prelude-
  source interpretation (needed for `map_keys`/`map_values`, whose
  dependency chain requires real pattern dispatch and recursion) is
  E24-4+ scope -- see `m0smith/genia-2026#968`.
- `src/global_env.hpp`, `src/evaluator.hpp` — the flat program-level
  environment (assignment/lookup) and Core IR evaluator.
- `src/render.hpp` — canonical Integer/Boolean/String/List display
  rendering for command/file mode's auto-display result.
- `src/ast_projection.hpp`, `src/ir_projection.hpp`, `src/engine.hpp` —
  wire projections for the `parse`/`lower` operations and the
  parse -> lower -> eval pipeline `eval`/`cli` share.
- `tests/test_bignum.cpp`, `tests/test_engine.cpp`, `tests/test_protocol.cpp`
  — Catch2 unit tests (internal genia-cpp tests, not shared conformance
  evidence).
- `genia-adapter` (the built binary) declares `parser`, `ast_lowering`,
  `cli_command_mode`, and `cli_file_mode` `supported`, `core_ir_eval`
  `partial`, and every other `spec/manifest.json` capability
  `unsupported`. Running the full shared spec corpus against it:
  `total=744 passed=21 failed=0 unsupported=723 protocol_error=0
  crash=0 timeout=0 invalid=0` — the 10 cases `docs/design/r24/
  bootstrap-cases.json` pins for E24-2+E24-3 all pass, plus 11
  incidental cases this slice's honest, evidence-matched grammar/
  lowering also happens to satisfy (e.g. `eval/assignment-rebind`,
  `eval/r19-unicode-debug-literal-non-control`,
  `ir/config-provider-ordinary-calls`,
  `parse/parse-config-provider-ordinary-call` -- each individually
  verified to genuinely exercise only this slice's implemented grammar,
  not a lucky accident).
- String storage/rendering is byte-transparent (copies UTF-8 bytes
  through unexamined), which correctly handles literal storage,
  equality, and display for any well-formed UTF-8 input, but is not yet
  genuine codepoint-aware iteration (no codepoint counting/indexing) --
  see `docs/design/r24/native-primitive-inventory.md`'s "UTF-8 decode/
  code-point iteration" primitive; that becomes necessary once a
  string-indexing/length function is in scope.
- Lambdas, pattern matching, Outcomes, Decimal/Rational/Float64, and
  open functions remain entirely unimplemented — that starts at E24-4
  (`genia-2026`'s
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
# total=744 passed=21 failed=0 unsupported=723 protocol_error=0 crash=0 timeout=0 invalid=0
```

Formatting/lint (matching the R24 dependency/toolchain policy):

```bash
clang-format --dry-run --Werror src/*.cpp src/*.hpp tests/*.cpp
clang-tidy -p build src/main.cpp src/adapter.hpp src/protocol.hpp
```

## Contributing

Read `AGENTS.md` before making any change here.
