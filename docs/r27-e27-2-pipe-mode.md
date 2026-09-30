# R27 E27-2 — C++ CLI pipe mode (capability boundary and evidence)

Authority: `m0smith/genia-2026` (`GENIA_STATE.md`, `GENIA_REPL_README.md`,
`spec/cli/*`, `spec/manifest.json`). Tracking issue:
[`m0smith/genia-2026#1038`](https://github.com/m0smith/genia-2026/issues/1038).
This document records what `genia-cpp` claims; it defines no Genia semantics.

## Claim

`cli_pipe_mode` is declared **`supported`** for the shared cases that carry
`requires: [cli_pipe_mode]` in `genia-2026` (pinned revision in `README.md`),
reached through the E16-1 `cli` operation as `-p <expr>` with piped stdin. E27-2
gated the 9 cases below; E27-5 broadened the gated set to 16 (see
`docs/r27-e27-5-hardening.md`). The 9 first-wave cases:

`pipe_mode_basic`, `pipe_mode_map_parse_int`, `pipe_mode_explicit_run_error`,
`error_pipe_mode_explicit_run`, `pipe_mode_collect_error`,
`pipe_mode_bare_parse_int_error`, `pipe_mode_sum_error`, `pipe_mode_argv_empty`,
`pipe_mode_bypass_main`.

All 9 pass (`total=772 passed=220 failed=0 unsupported=552 protocol_error=0
crash=0 timeout=0 invalid=0`; see `evidence.json`). The 11-case increase over
the E27-1 baseline (209) is those 9 plus two adjacent ungated cases
(`flow-error-propagation-sum-on-flow`,
`issue-306-seq-boundary-wrapped-list-one-item`); they are not part of the claim.

## Behavior (cross-checked against `genia -p`)

- The stage expression is parsed alone and must be one expression; a full
  program, an assignment, a function definition or an empty expression reports
  `Pipe mode expression must be a single stage expression, not a full program`.
- An unbound `stdin` or `run` reference (including as a callee) is rejected
  before anything runs; lambda parameters named `stdin`/`run` shadow them.
- The wrapped program is `stdin |> lines |> <expr> |> _pipe_run`, run with the
  span file name `<pipe>`. `main` is never dispatched and no result is displayed.
- Runtime errors are rewritten as `src/genia/errors.py`'s
  `_format_pipe_mode_error` does: non-Flow final result, per-item stage guidance
  (`Did you mean: map(x) or keep_some(x)`), reducer guidance
  (`Did you mean: collect |> x`), the generic Flow-stage message, and the
  single-use message. Output printed before an error stays on stdout.
- `argv()` is `[]` in pipe mode.

Alongside this, the pipeline-stage error wrapper now applies to every runtime
error raised in a stage (undefined names, runtime errors), as in the reference
host, not only Flow errors. This also corrects `-c` behavior such as
`1 |> nope`, which previously printed the unwrapped message.

## Not claimed

- `pipe_mode_record_pipeline_collect_validated_boundary_error`: needs
  `parse_jsonl_record` (`json_compat`, permanently Python-host-only). Ungated;
  C++ reports it `unsupported`.
- Trailing script arguments after `-p <expr>`; missing stdin.
- The reference host's Option-receiver (`received some`/`none`) and
  `collect_validated` rewrites; such runs are `unsupported`.
- `argv()` outside pipe mode; other CLI modes are unchanged.
- The single-use rewrite is implemented from the reference source but cannot be
  reached from one stage expression, so only its mapping function is unit-tested.
- Cross-release pipe cases (`r10-/r13-/r15-cross-mode-pipe`,
  `r11-model-fixture-pipe`, `validated_record_pipeline_killer_workflow`) are
  E27-5 hardening and remain `unsupported`.

## Verification

- `ctest --test-dir build` (includes `tests/test_pipe_mode.cpp`).
- `python -m tools.spec_runner --host build/genia-adapter --evidence evidence.json`
  from a `genia-2026` checkout at the pinned revision.
- A 42-program differential run against `genia -p` found no output difference
  for any program this host reports `ok` (the remainder are `unsupported`), and
  the 70-program E27-1 `-c` differential is unchanged.
