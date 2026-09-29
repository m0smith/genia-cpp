# R27 E27-1 — C++ Flow phase 1 (capability boundary and evidence)

Authority: `m0smith/genia-2026` (`GENIA_STATE.md`, `spec/flow/*`,
`spec/manifest.json`). Tracking issue:
[`m0smith/genia-2026#1035`](https://github.com/m0smith/genia-2026/issues/1035).
This document records what `genia-cpp` claims; it defines no Genia semantics.

## Claim

`flow_phase_1` is declared **`supported`** for exactly the 17 shared cases that
carry `requires: [flow_phase_1]` in `genia-2026` (pinned revision in
`README.md`):

| Slice | Cases |
|---|---|
| `stdin \|> lines`, `collect` | `stdin-lines-collect-basic` |
| bounded pull over stdin | `stdin-lines-take-early-stop` |
| `evolve` + `take` | `evolve-init-f-integer-progression`, `evolve-init-f-doubles-from-seed` |
| `map` / `filter` | `flow-map-basic`, `flow-filter-basic`, `flow-map-filter-chain` |
| single-use enforcement | `flow-single-use-error` |
| Seq-compatible terminals | `issue-306-seq-boundary-flow-{collect,each,run}` |
| `drop` / `take` | `issue-306-seq-boundary-flow-drop`, `seq-finalization-drop-take` |
| `scan` / `reduce` | `seq-compatible-flow-scan-{basic,bounded-evolve}`, `reduce-flow-{bounded-evolve,range-basic}` |

All 17 pass (`total=772 passed=209 failed=0 unsupported=563 protocol_error=0
crash=0 timeout=0 invalid=0`; see `evidence.json`). The 31-case increase over the
R26 baseline (178) is those 17 plus 14 adjacent, ungated shared cases that now
also pass (`each`/`collect`/`run` over lists, `stdlib-reduce-*`,
`flow-keep-some-parse-int`, `output-print`, `command_mode_collect_sum`,
`representation-flow-transport`, and similar). They are not part of the
`flow_phase_1` claim.

## Not claimed

- The rest of `spec/flow/*` (`tee`/`merge`/`zip`, `rules`/`refine`, `step_*`,
  Template, JSON, model and retrieval Flow compositions) stays `unsupported`.
- `cli_pipe_mode` (`genia -p`) is E27-2 and remains `unsupported`.
- Displaying a Flow value (`<flow ...>`), Flow equality, list-form `scan`,
  `take`/`drop` over lists, and `tee`/`merge`/`zip` are `unsupported`.
- HTTP server and outbound HTTP are not part of this slice.
- Unicode: `upper`/`trim`/`parse_int` decide ASCII input only. Any non-ASCII
  string reaching them is reported `unsupported`; Unicode case mapping,
  whitespace classification and digit parsing are not implemented.
- stdin containing a carriage return is `unsupported` (universal-newline
  translation is not evidenced).
- Pipeline-stage error spans name `<command>`; the file name used for stage
  errors under file mode is not evidenced, so that case is `unsupported`.

## Kernel behavior (all cross-checked against the Python reference host)

- A Flow is lazy, pull-based and single-use. A second consumption raises
  `Flow has already been consumed`; inside a pipeline it is wrapped as
  `pipeline stage N failed in <mode> at <stage> [<command>:L]: stage received
  <type>; <message>`.
- A downstream Flow consumes its upstream on its first pull only.
- `take(n)` never pulls more than `n` items; `evolve` runs its step only for
  demanded items after the seed. Two `stdin |> lines` Flows share one read
  position.
- Stage functions receive items directly; an item that is a `none` Outcome, a
  non-callable stage, a non-Boolean filter predicate, or a non-integer count is
  `unsupported` rather than guessed.
- Finalization: this kernel owns no closable resource (stdin is an in-memory
  cursor), so early termination is "stop pulling". Shared specs expose only
  stdout, stderr and exit code, so nothing finer is claimed.

## Verification

- `ctest --test-dir build` (includes `tests/test_flow.cpp`).
- `python -m tools.spec_runner --host build/genia-adapter --evidence evidence.json`
  from a `genia-2026` checkout at the pinned revision.
- A 70-program differential run against `genia -c` found no output difference
  for any program this host reports `ok` (the remainder are `unsupported`).
