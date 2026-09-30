# R27 E27-5 — Flow and pipe-mode hardening (boundary and evidence)

Authority: `m0smith/genia-2026` (`GENIA_STATE.md`, `spec/*`). Tracking issue:
[`m0smith/genia-2026#1047`](https://github.com/m0smith/genia-2026/issues/1047).
This document records what `genia-cpp` claims; it defines no Genia semantics.

## What this release step did

E27-5 adds **no capability**. It broadens and stress-tests the two shipped ones,
`flow_phase_1` and `cli_pipe_mode`, with shared evidence whose expectations were
generated from the Python reference host (`genia -c` / `genia -p`), not hand-typed.

- 21 new shared cases: 12 Flow (`requires: [flow_phase_1]`), 2 pipeline-stage
  error cases (ungated: pure pipeline semantics), 7 pipe-mode
  (`requires: [cli_pipe_mode]`).
- 8 existing cases newly gated under `flow_phase_1` (`flow-error-propagation-sum-
  on-flow`, `flow-keep-some-parse-int`, `representation-flow-transport`,
  `seq-compatible-evolve-each-run`, `count-as-pipe-stage-type-error`,
  `seq-compatible-flow-each-preserves-items`, `seq-compatible-flow-transform-
  chain`, `count-flow-basic`).
- Result: `flow_phase_1` has 37 gated cases and `cli_pipe_mode` 16; **all 53 pass**
  (`total=793 passed=257 failed=0 unsupported=536 protocol_error=0 crash=0
  timeout=0 invalid=0`; see `evidence.json`).

## Enabling changes

Only what those cases needed and Python defines: the Seq-compatible diagnostics
for `collect`/`run`/`each`/`scan`/`reduce`/`map` on non-list, non-Flow values (with
the bare-`stdin` hint), `keep_some expected a flow`, `_seq_type_error`, string
`concat` (two strings), and the verbatim prelude `count`.

## Not claimed / still unsupported

- **Parser gap:** a pipeline inside call arguments (`print(x |> f)`) is not parsed by
  the C++ host and is reported `unsupported`. It is a grammar gap, not a Flow gap;
  the affected shared case is written with assignments instead.
- `tee`/`merge`/`zip`, `rules`/`refine`/`step_*`, `rand_flow`, list-form `scan`,
  `reduce` with an Option accumulator (needs the `none` literal), and Flow display.
- Python-host-heavy cross-release cases stay unsupported: `r10/r11/r13/r15-cross-
  mode-*`, `validated_record_pipeline_killer_workflow` (config views, model
  fixtures, Templates, `json_compat`), and the Template/JSON/model Flow cases.
- Ungated cases C++ now also passes (the `seq-compatible-*-nonseq-error` and
  `issue-306-seq-boundary-*-error` eval cases) are base-corpus cases, not part of
  a Flow claim.

## Gap freshness

At the pinned revision the host parity gate reports `flow_phase_1` and
`cli_pipe_mode` `PARITY_OK`, and `http_server` and `http_outbound_transport`
`KNOWN_GAP` (deferred by E27-3/E27-4). No `STALE_GAP` or `UNDOCUMENTED_GAP`.

## Verification

- `ctest --test-dir build`, including new hardening tests in `tests/test_flow.cpp`
  and `tests/test_pipe_mode.cpp`.
- `python -m tools.spec_runner --host build/genia-adapter --evidence evidence.json`
  from a `genia-2026` checkout at the pinned revision.
- Differential runs against the Python host: 89 `-c` programs (83 identical, 6
  unsupported, 0 differences) and 45 `-p` programs (42 identical, 3 unsupported,
  0 differences). The differential harnesses found no output difference for any
  program this host reports `ok`.
