# R25 private quiescence accounting

The C++ R25 evidence fixture waits for accepted Cell updates and Process messages,
including causally committed nested sends. This is host-private test provisioning,
not a public Genia wait operation. `cell_get` still reads the latest committed
state without draining work. The semantic authority is genia-2026 STATE and its
R25 stateful-runtime/concurrency contract and concurrency-evidence design.

`R25Quiescence` counts accepted items across both entity types. Admission is
recorded under the entity mutex before its worker can observe the queue item.
A successful parent remains outstanding while its staged actions run, outside
that mutex. Child admission therefore precedes parent retirement. A zero count
is a causal fixed point even for alternating entity types and targets created
inside callbacks; it does not depend on a registry snapshot or fixed sweeps.
Unrelated producers admitting work after zero is observed are outside the wait's
guarantee. The condition wait has no polling or semantic time limit.

Each entity also retains its local accepted/completed counters. Local completion
now includes committed staged dispatch, but does not itself wait for descendant
execution. The combined fixture and either `await_all_idle` helper use the shared
condition instead. Failure discards queued work and staged sends, preserving the
existing last-good-state/error boundary. Restart retires only queued old-generation
items; an older in-flight callback remains outstanding until its stale result is
discarded. A committed parent's dispatch remains outstanding across a later
restart. Stop still rejects admission and drains accepted work.

Destruction requests termination and joins the active worker, then retires any
remaining queued items that will never execute. The shared counter owns no
handles and does not keep entities alive. Existing lifetime restrictions remain:
callbacks can block destruction indefinitely, ownership cycles must be avoided,
and the final owner must release off-worker. Native staged-action exceptions
retain their existing uncaught behavior; this correction does not introduce
retry, error recovery or a public transaction mechanism.

Lock order is entity mutex then accounting mutex. The accounting mutex is never
held while acquiring an entity mutex or running a callback/action. Self-send is
therefore admitted outside the sender's mutex. The private fixture remains
unavailable in ordinary evaluation. No capability, protocol, Core IR or semantic
contract revision changes accompany this accounting repair.

Regression coverage is in `tests/check_quiescence.cpp`: paused Cell and Process
dispatch, the exact local/shared completion predicates, alternating newly created
targets, stale restart and stopped draining, failure rollback, self-send, and
queued-work retirement during destruction. The original shared
`r25-cell-nested-send-transaction` expectation remains `[1, 15, true]`.
