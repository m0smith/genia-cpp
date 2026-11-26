# R31 X Part B ordinary-call none propagation

C++ ordinary calls return the first absence argument unchanged unless the
resolved callable handles absence. Dispatch checks the selected fixed-arity
stratum for Option patterns and recognizes direct native awareness and
final-expression delegation under the pinned reference rules. A pipeline
skips its remaining stages on either absence or error. Call-shaped stages
also apply ordinary argument propagation. List elements can contain pipelines.

Raw argument delivery remains available through `apply_raw`, list callbacks,
and the relevant native Flow callbacks. Flow map, filter, evolve, and scan use
ordinary call admission. Flow filter skips an absence verdict. The `err`
constructor retains its ability to carry an absence as its reason. A missing
absence-pattern context is an internal null during the body and normalizes at
the user-call return boundary.

The executable declaration subset covers local unguarded fixed-arity ordinary
headers containing an absence pattern. Such declarations share the callable
identity and declaration order of the selected arity with ordinary fallbacks.
Duplicate pattern clauses use alpha-normalized R20 keys. This is early
delivery of part of [#1269](https://github.com/m0smith/genia-2026/issues/1269);
`default_open_functions` remains unsupported. General pattern and guard
execution, metadata, cross-module linking, immutable bindings, closed-function
work, and varargs strata remain outside this slice.

The C++ contract pin is the frozen shared-spec commit
`8ad371551b0de2ada39bf39266066e918b966200`. All ten #1347 shared cases pass.
Of the six existing R20 Option cases, the Err pipeline case passes. The
delegation, none-stratum, and other-arity cases require the bare absence
expression; the some-stratum case additionally exercises Some lifting; and
the varargs-stratum case requires varargs. Those five are unsupported under
the existing hidden-partial open-function work tracked by #1018.

See [evidence.json](../evidence.json) and the
[#1347 ledger](https://github.com/m0smith/genia-2026/issues/1347) for the
full shared-corpus result and provider integration boundary.
