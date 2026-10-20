# Issue #51 implementation and verification handoff

Status: independent Claude maintainability review and behavioral/documentation
truth audit both returned **PASS WITH NON-BLOCKING FINDINGS** at exact audited
head `af0239984ca6f9725657e87596e25c23a2eafc4e` (links below). These verdicts
apply to that head, not to subsequent documentation corrections. Independent
documentation-truth spot-check and final-head CI remain required. No merge or
issue closure.

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
- `af02399`: evidence-only commit changes no build input from `793f100`.
  That tested source and its historical build-input digest are recorded in
  `issue-51/verification.json`. Subsequent distillation adds header comments,
  so that digest is not a digest of the corrected documentation head.

## Implementer-reported validation (macOS and Docker)

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

## Independent Claude verdicts and remaining gates

Both verdicts audit exact `af0239984ca6f9725657e87596e25c23a2eafc4e`:

- [Maintainability/correctness review — PASS WITH NON-BLOCKING FINDINGS](https://github.com/m0smith/genia-cpp/pull/52#issuecomment-6297673785).
  Independently reproduced Linux results include 3/3 CTest targets, GCC/Clang
  adversarial repetitions, GCC sanitizers and mutation checks, pinned R16 corpus
  257 pass / 536 unsupported with zero failure classes, and the unchanged R25
  case passing 12,000/12,000 corrected runs. Baseline stress reproduced failures.
- [Behavioral/documentation truth audit — PASS WITH NON-BLOCKING FINDINGS](https://github.com/m0smith/genia-cpp/pull/52#issuecomment-6301362049).
  Independent Linux clang/GCC execution includes 3/3 CTest targets, 300 check
  invocations, GCC sanitizer checks, and 500/500 unchanged R25 passes at each of
  the exact contract pin and main `21691ff`. The pre-fix control at main had
  481 pass / 19 fail. RED reproduced its two failed assertions.

Neither reviewer reproduced the implementer's macOS/Apple Clang or Docker
results above. The maintainability review reproduced the full pinned corpus;
the separate truth audit did not rerun either full corpus. Repetitions and
sanitizers are regression evidence, not exhaustive concurrency proof or
comparative failure-rate claims. Review comments retain the detailed independent
binary, compiler, scheduling and evidence limits.

Post-audit corrections address N2 (C++ self-wait limitation in host docs and
wait comments) and N3 (durable README wording). Genia staged actions are
send-only; no public language restriction is added. N1's unsigned-counter guard
remains deferred as independently classified nonblocking; no executable logic,
test, capability, protocol or contract pin changes are part of this distillation.
Latest authorities read at genia-2026 `21691ff818f914e0f39ce1bb138178d7773ca865` remain accurate for this
host-private clarification; no root-authority or MCP semantic edit is needed.

Stop for an independent documentation-truth spot-check of the corrected head
and require final-head CI. The earlier verdicts do not certify the new prose.
Keep #51 open and PR #52 draft. Main-repository corrected-host pin promotion
and required Docker/host-parity workflows remain separate integration gates;
this distillation neither performs nor certifies them. Do not begin Slice C.

## Post-audit distillation validation

- Pinned (`469748098f…`) and current (`21691ff8…`) code-documentation providers:
  552 records / 276 legacy gaps / 2 exclusions / 0 errors against `bb7c747`.
- Authority-sync tests: 244 passed / 34 skipped. Strict public-doc coverage lint:
  24 files / 286 doc strings / 0 errors / 0 warnings.
- Authoritative documentation `mkdocs build --strict` at `21691ff8…`: exit 0;
  no authoritative source edits.
- Full first-party C++ formatting check (`clang-format` 23.1.3): exit 0.
- Scope/integrity checks: exactly six documentation/evidence/comment files;
  C++ additions are comments only, N1/tests/protocol/capabilities and historical
  evidence values unchanged, RED/GREEN ancestry intact, `git diff --check` clean.

These are correction validation results, not an independent documentation-truth
verdict. Final-head CI and the independent spot-check remain separate gates.
