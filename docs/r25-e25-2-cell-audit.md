# R25 E25-2 C++ Cell Skeptical Audit

Status: **PASS**, for E25-2 only. This branch is stacked on C++ Ref PR #21
and authority PR `m0smith/genia-2026#1007`; it is not an R25 completion
verdict.

## Declared boundary

- `refs`: `supported`
- `cell_primitives`: `supported`
- `process_primitives`: `unsupported`
- Actor: absent and excluded from R25

The adapter remains on protocol version 1 and the existing `eval` operation.
The `r25_concurrency` fixture alone enables `_r25_await_idle`; ordinary source
cannot call that private evidence hook.

## Evidence

- Native build and CTest: pass, 165 cases / 698 assertions.
- Shared external-host run: `761 total / 145 pass / 616 unsupported`, with
  `failed=0`, `protocol_error=0`, `crash=0`, `timeout=0`, and `invalid=0`.
- Evidence file: `/tmp/r25-e25-2-cpp-evidence.json`.
- `clang-format --dry-run --Werror`: pass.

All three Cell shared cases newly become applicable and pass: FIFO failure
preservation, restart generation isolation, and nested-send commit/discard.

## Skeptical checks

- Cell identity is a shared opaque entity; equality compares handles and
  rendering does not expose addresses or state.
- One worker serializes accepted updates FIFO. Accepted/completed counters
  account for committed, failed, stale-generation, and discarded queued work.
- A failed update preserves the last committed Ref value, records only the
  stable text `cell update failed`, rejects later observation/sends, and
  discards its staged sends.
- Restart increments the generation before installing the replacement state;
  an older callable may finish but cannot commit state or nested sends.
- Nested Cell sends are held in thread-local transaction state and accepted in
  program order only after the enclosing replacement commits.
- Existing block syntax now lowers through the established `IrBlock` node; no
  host-only callable or new portable node was introduced.
- The global evidence registry is weak, so it does not extend Cell lifetime.
  Destruction joins the owned worker, avoiding detached-thread use-after-free.

Process and cross-primitive Cell-to-Process transactions intentionally remain
for E25-3. Sanitizer and broader stress coverage remain E25-4 gates.

## Verdict

**PASS.** E25-2 is independently reviewable after its two stack predecessors.
E25-3 must remain stacked until the authority, Ref, and Cell changes merge.
