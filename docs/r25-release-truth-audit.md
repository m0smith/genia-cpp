# R25 Release Truth Audit

Verdict: **PASS — release candidate ready for ordered merge.** This verdict
does not self-merge or pretend the stacked branches are already on `main`.

## Exact evidence

- Authority revision: `9ab0d3a323c9add045d47db1f476aead96af1e77`
- Protocol: `1`
- Shared suite: `762 total / 149 pass / 613 unsupported`
- Failure taxonomy: `fail=0`, `protocol_error=0`, `crash=0`, `timeout=0`,
  `invalid=0`
- Checked-in artifact: `evidence.json`
- Supported R25 gates: `refs`, `cell_primitives`, `process_primitives`

## Falsification result

The audit found one real evidence and implementation defect: shared cases
proved asynchronous effects but did not observe the return values of
`cell_send` and `send`; C++ returned noncontractual placeholders. Authority
commits `d2de7b7`/`9ab0d3a` add the missing deterministic-exit shared case, and C++ commit `b0740de` repairs
both calls to return exact `none("nil")`. Python and C++ pass the new case.

The re-audit found no timing thresholds, sleeps, Actor/ActorRef surface,
supervision, distribution, placement, scheduler/fairness promise, thread
identity leak, raw native exception text, new protocol operation, or second
evidence framework. Unsupported surfaces remain unsupported rather than being
converted to passes.

Release, sanitizer, ThreadSanitizer, formatting, clang-tidy, deterministic
stress/lifecycle, private-fixture isolation, and shared external-host gates all
pass. Documentation keeps C++ explicitly bounded rather than implying Python
feature parity.

## Required merge order

1. `genia-2026#1007` — E25-0 authority/evidence
2. `genia-cpp#21` — E25-1 Ref
3. `genia-cpp#22` — E25-2 Cell
4. `genia-cpp#23` — E25-3 local Process
5. `genia-cpp#24` — E25-4 hardening
6. C++ E25-5 completion PR
7. authority E25-5 truth/audit PR

R25 is complete only after that ordered merge finishes. Actor remains R38.
