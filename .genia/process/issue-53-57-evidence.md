# C++ conformance repair evidence — #53–#57

The nine original failures pass through the unchanged E16-1 adapter protocol and
shared comparator. No capability, shared oracle, normalizer, test-selection or
known-host-gap changes were used.

## Provenance

- Baseline C++: `44456baea06b4bb7bdf727e39939dd7150261ee6`.
- Authority suite: `aebaa9949fcdc8fa4fbe9abfdb175808f6982186` (exact merge revision
  from [failing run 40582524750](https://github.com/m0smith/genia-2026/actions/runs/40582524750)).
- Dedicated C++ branch: `issue-53-57-host-conformance`.
- RED commits: `ffb9b80` (eight groups fail; 36 failing assertions) and `653a2b4`
  (remaining named-block and string-expression guards).
- Final native regressions live in `tests/check_host_repairs.cpp`. They were
  moved from Catch2 to a plain executable, matching the repository's existing
  header-capture/quiescence checks, so the canonical documentation checker can
  inspect them. Existing Catch2 tests retain their original source.

## Original failure matrix

`stdout`/`stderr` below are the unchanged shared oracle; CLI comparison uses the
runner's existing trailing-newline normalization. Every row now reports PASS.

| Shared test ID | Capability | Expected | Baseline actual | Root cause / correction | Issue |
| --- | --- | --- | --- | --- | --- |
| model-b-c-hdr-grouped-body-captures-header | ungated | stdout '[0, 10]\n'; exit 0 | empty stdout; Undefined name: x; exit 1 | Grouped closures omitted positional header bindings. Bind ordinary header names before arm bindings; preserve explicit-open capture refusal. | #53 |
| model-b-c-meta-inline-doc-body-runs | ungated | stdout '2\n'; exit 0 | empty stdout; Undefined name: x; exit 1 | The parser treated the leading inline string as a body expression. Carry same-line short inline documentation as inert AST/IR metadata; validate canonical metadata before group mutation. | #55 |
| model-b-c-main-repl-never-selects | repl | stdout '0\n42'; exit 0 | empty stdout/stderr; exit 0 | The lexer rejected semicolons and REPL completeness buffering silently dropped the submission; named block bodies and persistent arity identity were also missing. Parse separators and required named blocks, retain shared arities/aliases, echo final values without implicit REPL main dispatch. | #57 |
| model-b-c-open-persistent-arities | repl | stdout '0\n0\n3'; exit 0 | empty stdout/stderr; exit 0 | The lexer rejected semicolons and REPL completeness buffering silently dropped the submission; named block bodies and persistent arity identity were also missing. Parse separators and required named blocks, retain shared arities/aliases, echo final values without implicit REPL main dispatch. | #57 |
| model-b-c-open-repl-alias-sees-later-arity | repl | stdout '0\n0\n0\n3'; exit 0 | empty stdout/stderr; exit 0 | The lexer rejected semicolons and REPL completeness buffering silently dropped the submission; named block bodies and persistent arity identity were also missing. Parse separators and required named blocks, retain shared arities/aliases, echo final values without implicit REPL main dispatch. | #57 |
| model-b-c-d8-legacy-identical-header-dup | ungated | stderr 'Error: Duplicate function definition: describe/1\n'; exit 1 | stdout "later"; empty stderr; exit 0 | Environment installation replaced an ordinary closure instead of retaining arities and rejecting duplicates. Accumulate distinct fixed arities in one shared callable; reject duplicate headers before later effects. | #56 |
| model-b-c-d8-r20-flattening-unchanged | open_functions | stderr "Error: open-function-duplicate-clause: f in unit 'base' has two clauses with an identical dispatch key shape=('fixed', 1) (first at <command>:1, duplicate at <command>:2)\n"; exit 1 | stdout 1; empty stderr; exit 0 | Open closures had no distinct identity/alias protection or structural duplicate-key validation. Keep explicit-open identity, reject ordinary replacement, validate alpha-normalized keys before installation with original spans. | #54 |
| model-b-c-key-legacy-dup-id | ungated | stderr 'Error: Duplicate function definition: f/1\n'; exit 1 | stdout 2; empty stderr; exit 0 | Environment installation replaced an ordinary closure instead of retaining arities and rejecting duplicates. Accumulate distinct fixed arities in one shared callable; reject duplicate headers before later effects. | #56 |
| model-b-c-own-explicit-open-alias-unchanged | open_functions | stderr 'Error: Cannot define function a/2: name is bound to an open function interface or linked view, which an ordinary definition cannot replace\n'; exit 1 | stdout 3; empty stderr; exit 0 | Open closures had no distinct identity/alias protection or structural duplicate-key validation. Keep explicit-open identity, reject ordinary replacement, validate alpha-normalized keys before installation with original spans. | #54 |

## Shared verification

| Host / stage | Total | PASS | FAIL | UNSUPPORTED | Protocol error / crash / timeout / invalid |
| --- | ---: | ---: | ---: | ---: | --- |
| C++ baseline | 1044 | 270 | 9 | 765 | 0 / 0 / 0 / 0 |
| Repaired C++ | 1044 | 293 | 0 | 751 | 0 / 0 / 0 / 0 |
| Python protocol reference | 1044 | 1026 | 0 | 18 | 0 / 0 / 0 / 0 |

All nine original FAIL cases become PASS. Fourteen previously unsupported cases
also execute and pass. No baseline passing case becomes unsupported:

- `command_mode_argv`
- `command_mode_double_dash`
- `error-r20-local-duplicate-clause`
- `funcdef-block-body`
- `model-b-b-parameter-remains-mutable`
- `model-b-c-d8-grouped-decl-one-key`
- `model-b-c-main-command-mode`
- `model-b-c-main-legacy-main0-fallback`
- `model-b-c-main-legacy-main1-preferred`
- `model-b-c-main-no-qualifying`
- `model-b-c-open-alias-sees-later-arity`
- `model-b-c-open-noncontiguous-arities`
- `model-b-c-own-alias-call-live`
- `model-b-c-own-entry-prelude-name`

The additional cases exercise the same repaired ordinary arity/alias, block,
command argument/main selection and explicit-open duplicate paths. This does not
claim Python Slice C, default-open dispatch, annotations or immutable bindings.
The unchanged host-parity gate reports **OK** against fresh evidence from both
hosts. This is an actual local gate invocation, not the skipped gate step in the
original failing workflow.

## Commands and review boundaries

From the authority worktree, use its existing environment with
`UV_PROJECT_ENVIRONMENT=/workspace/genia-2026/.venv` and a writable
`UV_CACHE_DIR=/workspace/work/uv-cache`:

```sh
uv run --no-sync python -m tools.spec_runner --host /workspace/work/cpp-clean-build/genia-adapter --evidence /workspace/work/cpp-evidence.json
uv run --no-sync python -m tools.spec_runner --host-jobs 8 --host 'python -m hosts.python.protocol_adapter' --evidence /workspace/work/python-evidence.json
uv run --no-sync python -m tools.spec_runner.host_parity_gate --python-evidence /workspace/work/python-evidence.json --cpp-evidence /workspace/work/cpp-evidence.json --known-gaps spec/known_host_gaps.json
```

`ctest --test-dir build --output-on-failure` includes the existing Catch2 suite,
R20 header-capture guard, R25 quiescence checks and the new native regressions.
Formatting, clang-tidy and the canonical documentation baseline ratchet are
also run; their exact final outcomes are attached to the PR handoff.
The ratchet only removes resolved debt; no baseline entry or exception is added.

## Integration and documentation

The C++ README and `docs/host-conformance-repairs.md` describe only the verified
bounded implementation. The separate language-repository proposal updates its
C++ host/REPL wording, relevant function chapter and capability matrix, with
`GENIA_STATE.md` at 42,996 words against its 43,000-word gate. RULES, semantic
facts and the MCP profile require no semantic changes; targeted documentation
checks report 244 passed / 34 skipped and the MCP generator check passes.

The C++ declared semantic contract pin remains `6402e668…`. The new-suite evidence
is therefore **current-main compatibility**, not a claim of a changed adapter
contract pin. The deliberately prepared workflow pin proposal must name the
published repair commit and be reviewed separately. Do not merge #1270, either
repair/integration branch, or close #53–#57 without the required independent
maintainability and behavioral/truth review. Hosted CI and `@codex review`
status are recorded separately from these local checks.

## Maintainer review focus and limits

- Inspect declaration validation before mutation, exact error text and original
  clause spans, ordinary-header versus explicit-open binding, alias identity,
  argument/main mode separation and unchanged R22/R27 behavior.
- The parser now stops after a committed declaration fails; it cannot discard
  an invalid declaration prefix and silently accept a later call.
- Source-bearing duplicate diagnostics require a real source name. Adapter eval
  and CLI supply it. The internal direct-engine API preserves its existing
  unsupported boundary when no diagnostic provenance is supplied.
- Guards/varargs headers, nested open declarations, cross-module linking,
  triple-quoted inline docs and full doc/help remain outside this bounded host.
- The existing shared-pointer environment/closure ownership model is retained;
  overloads do not add a self-pointer, scheduler or global registry.
- No independent review verdict is implied by implementer verification. Issues
  remain open; a successful final-head hosted parity run requires deliberate
  adoption of the proposed C++ SHA after review.

## Final clean-build handoff

The final build used a new `/workspace/work/cpp-clean-build` directory to avoid
reusing products from interrupted builds. All four CTest targets pass, including
ten native repair groups. The clean binary passes both the 1,044-case suite
(293 / 0 / 751) and the declared pinned 793-case suite (261 / 0 / 532), with
protocol_error/crash/timeout/invalid all zero in both runs. The nine-case protocol
replay separately reports nine PASS results. The fresh local parity gate is OK.

Formatting and `git diff --check` pass. `clang-tidy` exits 0 with 519 project
warning diagnostics (the warning backlog is not claimed resolved). Canonical
code-documentation validation passes: 553 records, 264 remaining legacy gaps,
two classified exclusions, zero errors; twelve resolved baseline entries were
removed and no entries/exceptions added. Authoritative documentation checks:
244 passed, 34 skipped; MCP generator `--check` passes. This is implementer
verification, not the required independent review verdict.
