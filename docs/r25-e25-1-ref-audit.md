# R25 E25-1 C++ Ref Skeptical Audit

Status: **PASS** for issue `m0smith/genia-2026#1002`, temporarily stacked on
authority PR `m0smith/genia-2026#1007`. This is not an R25 completion verdict.

## Evidence

- pinned authority revision:
  `02f4caa15371f3ab6670de446cc4150debfe318e`
- protocol version: `1`
- `refs`: `supported`
- `cell_primitives`: `unsupported`
- `process_primitives`: `unsupported`
- full R16 totals: `761 total / 142 pass / 619 unsupported`
- failure classes: `fail=0`, `protocol_error=0`, `crash=0`, `timeout=0`,
  `invalid=0`

The one new independently applicable Ref case passes. The blocking producer
case correctly remains unsupported because it also requires
`process_primitives`; C++ unit tests prove blocking/wakeup mechanics meanwhile,
but are not presented as cross-host evidence.

## Adversarial findings

- Lost wakeup: the wait predicate and set notification are protected by one
  mutex/condition-variable state; the consumer test begins before the producer
  sets and observes the exact value.
- Lost update/data race: two native threads perform 400 total serialized
  updates and the exact final value is 400.
- Ownership: Genia Ref values carry `shared_ptr<Ref>` identity. An executing
  operation owns its Ref, preventing destruction/use-after-free while blocked.
- Copy/move: copying a Genia Ref value copies entity identity rather than
  contents; equality compares the shared entity pointer and never dereferences.
- Callback boundary: `ref_update` deliberately invokes the updater once inside
  the exclusive Ref update boundary. This matches the authoritative contract
  and preserves atomic exactly-once replacement. Same-Ref re-entry is explicitly
  outside the supported portable contract; no broader lock-order guarantee is
  claimed.
- Host leakage: no address, native thread identity, lock type, exception class,
  or native exception text becomes a Genia value or shared result.
- Capability honesty: only `refs` was promoted. Cell, Process, Actor, and every
  R26+ surface remain unsupported.
- Timing: no correctness assertion uses a sleep, duration, wake-latency bound,
  or scheduler-fairness claim.

## Validation

- Release build: pass
- CTest: pass
- clang-format gate: pass
- full external-host R16 run: pass under the zero-failure invariant
- `git diff --check`: pass

Clang-tidy and sanitizer breadth remain part of the E25-4 cross-cutting gate;
their absence here is not used to waive any observed failure.

## Verdict

**PASS.** E25-1 is independently reviewable and mergeable after authority PR
#1007. E25-2 must remain stacked until both predecessors merge.
