# Issue #51 implementation and verification handoff

Status: implementer validation only. Independent Claude maintainability review
and truth audit are pending. No self-review verdict, merge or issue closure.

## History

- RED `aa25d9a`: separate committed promise-paused action reproducer. The local
  completion predicate incorrectly read settled while a committed action was
  paused; the observed follow-on target was still 10 rather than 15. Earlier
  recorded execution had 1 failing test, 3 passing and 2 failing assertions.
- GREEN `ca10d4c`: references RED explicitly; successful work completes only after
  staged dispatch. One shared condition counter covers Cell/Process causal chains.
- `62b9238`: retain the seven schedules/33 assertions in a standalone native target,
  following the existing header-check target. The pinned checker does not parse
  Catch2 TEST_CASE macros. No assertion was weakened or baseline exemption added.
- `793f100`: host README and durable accounting/lifetime documentation.
- This handoff/evidence commit changes no build input. The tested source and
  build-input digest are recorded in `issue-51/verification.json`.

## Validation observed

| Check | Native macOS | Linux development image |
| --- | --- | --- |
| Release CTest | 3/3 targets pass | 3/3 targets pass |
| Adversarial standalone | 7 schedules / 33 assertions / 0 failures | same, plus 100 invocations at 8 workers |
| Pinned `6402e668` corpus | 257 pass / 536 unsupported | same |
| Main `f2670d4` corpus | 268 pass / 606 unsupported | same |
| Unchanged shared nested-send case, pinned serial | 1000/1000 pass | 1000/1000 pass |
| Unchanged shared nested-send case, main at 8 workers | 1000/1000 pass | 1000/1000 pass |

Every R16 failure-class count is zero. Four canonical R16 evidence documents and
binary/protocol/scheduling metadata are committed under `issue-51/` alongside
this file. The shared case SHA-256 remains
`935738ea0406c3249688352f3680ae01e5a3809279c381f6fe0e43976b39cc5a`,
with unchanged expected stdout `[1, 15, true]\n`. It was never reclassified.
Repeated schedules are regression evidence, not comparative failure-rate,
performance or exhaustive concurrency claims.

Native binary SHA-256:
`2f70303fcb1ff8482029f336534c72317ca171fcdcec988418a9b697309d8e0c`.
Docker binary SHA-256:
`9d4687dfcc2300aac9f2226c50c04ecc43270341b56f6cf3d40d88bec061e81c`.
Both actual executables return contract revision
`6402e668ae85a1e7efe94b3ac3d51f233b730ca4`, protocol `1`.
The pin in README/protocol source is deliberately unchanged. Only runs from
that exact contract checkout are pinned conformance. Runs from `f2670d4` are
`resolvable_ancestor` current-main compatibility, not current-pinned parity.
Earlier checks from `979302c` also passed; the final current-main evidence uses
`f2670d4`. Those authority files and R25 contracts are byte-unchanged across them.

Docker image ID and compiler versions are in the metadata. `clang-format` over
all first-party C++ source/tests exited 0. `clang-tidy` over the existing CI entry
points exited 0, with 510 emitted project warnings; this is not a warning-free
claim. Local raw report: `/tmp/cpp51-clang-tidy.log`. The native compiler also
reported existing Flow unused-capture and IR Some-switch warnings. Canonical code
documentation checker: 552 records, 276 legacy gaps, 2 exclusions, 0 errors; the
baseline/exemption files are unchanged.

## Reproduction

Build and run in this C++ checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
build/genia-quiescence-check
```

Build the main repository's `docker/Dockerfile.dev` image and bind this checkout
at `/workspace/genia-cpp`, using a separate `build-docker` directory. Run the same
commands there with that build directory. Full corpus evidence uses the existing
runner from separate Genia checkouts at `6402e668...` and `f2670d4...`:

```sh
uv run python -m tools.spec_runner --host /absolute/path/genia-adapter --evidence evidence.json
```

For the repeated unchanged case, the validation used the existing APIs
`tools.spec_runner.loader.load_spec`, `protocol.fetch_capabilities`, and
`host_executor.execute_spec_via_host`. It selected only
`spec/eval/r25-cell-nested-send-transaction.yaml`, fetched the actual binary's
capabilities, and required the runner result `pass` on each of 1,000 invocations,
using one worker at the pin and eight at main. There is no replacement runner,
custom expected output or fixture/capability bypass. Metadata records imported
protocol module paths and runner HEADs to avoid accidentally using another
worktree's Python modules. Adapter subprocess timeout is the existing harness
hang guard, not asserted scheduling behavior. Native gates use promises/conditions;
no sleep, settling delay or fixed sweep count was added.

## Authority and synchronization

Read latest AGENTS in both repositories and the four root authorities, Cell and
R25 evidence contracts, host guidance and maintained-code documentation rules.
This corrects private host/fixture accounting to the existing contract. It adds
no verified public language/runtime semantics, public wait API, capability,
protocol or Core IR change. Thus GENIA_STATE.md, GENIA_RULES.md,
GENIA_REPL_README.md and README.md in genia-2026 need no semantic edits for this
candidate. Relevant book/cheatsheet public semantics and MCP guidance remain
unchanged; the host README and `docs/r25-quiescence.md` document the actual repair.

After independent review, m0smith integration owns promoting the reviewed C++
revision into main-repository host/CI references and rerunning the main Docker
workflow on that corrected revision. Relevant surfaces are
`docs/host-interop/HOST_CAPABILITY_MATRIX.md`, `.github/workflows/host-parity.yml`
and `.github/workflows/docker-dev-environment.yml`. Their main C++ checkouts
still point to merged main; no unreviewed pin was promoted and no main-repo
workflow success for this unmerged candidate is claimed. Local Docker validation
above is distinct from that remaining integration/CI gate.

Any follow-up Codex implementation prompt must explicitly require synchronization
of GENIA_STATE.md, GENIA_RULES.md, GENIA_REPL_README.md and README.md to verified
behavior changes, together with applicable shared specs, host documentation,
book/cheatsheets and generated MCP guidance. Do not infer conformance from
ancestor-pinned evidence or expand the portable contract while fixing this host.

## Independent Claude gate

Review the actual diff from `bb7c747...`, not an implementer certification.
Inspect admission-before-visibility, dispatch-before-retirement, generation and
queue discard accounting, destruction/join retirement, lock ordering, dynamic
target/alternating entity chains, public diagnostic/transaction preservation and
fixture privacy. Verify the paused predicates and cleanup oracles, rerun the
unchanged shared case and check actual executable provenance. Existing native
staged-action exceptions remain uncaught; existing off-worker final-owner and
ownership-cycle restrictions are documented rather than silently redesigned.
No review or audit PASS is supplied by this handoff. Keep #51 open and the PR
draft until independent review and required CI/integration confirmation.
