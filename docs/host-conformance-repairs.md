# Existing supported-behavior repairs (#53–#57)

This host retains its bounded R27 capability floor. These corrections implement
existing Genia contracts; they do not implement Python Slice C or declare
`default_open_functions`, `immutable_bindings`, or `doc_help` supported.

Documented shared fixtures may contain `#` line comments. The tokenizer ignores
them outside strings through the next newline or end of source, preserving
original diagnostic lines. Ordinary inline function docstrings remain inert
metadata; `@doc` annotation execution remains unsupported. The native
`genia-source-comments-check` target covers these boundaries and the ordinary
main-selection shapes used by the documented shared guards.

- Ordinary grouped bodies bind their identifier headers positionally before
  arm-local bindings. Explicit-open arms retain the separate R20 rule: headers
  do not bind, and free header capture remains refused.
- Named ordinary fixed arities accumulate in one shared callable identity,
  including across successful REPL submissions and through existing aliases.
  Duplicate identifier headers reject before later statements execute.
- Explicit-open aliases cannot be replaced by ordinary declarations. Local
  duplicate detection compares structural, alpha-normalized, span-free patterns
  and retains original clause lines in the normalized error. Varargs
  headers, nested open declarations and cross-module linking remain unsupported.
- A short inline string followed by a same-line body is inert docstring metadata,
  carried through AST and portable IR. Identical metadata across arities is valid;
  conflicting metadata rejects before installation. String-only bodies, string
  expressions and string-literal arm patterns retain their existing meanings.
  This is not a `doc`/`help`, annotation, or triple-quoted-docstring implementation.
- Complete semicolon-separated REPL programs evaluate and echo their final value;
  successful bindings persist, failed submissions recover, genuinely incomplete
  input at EOF stays inert, and REPL evaluation never implicitly calls `main`.
- Named nonempty block bodies needed by that REPL evidence are accepted. In
  command/file CLI mode, source evaluation follows the existing ordinary
  `main/1`-before-`main/0` entrypoint rule; command arguments are passed as strings.
  Pipe/eval/REPL modes do not invoke that convention. File trailing arguments,
  pattern-headed/default-open main selection and unsupported body syntax remain
  outside this implementation.

The adapter supplies real diagnostic provenance (`<command>` or the file path).
The internal direct-engine API keeps span-bearing open-duplicate errors
unsupported when its caller supplies no source name, rather than inventing one.
This does not downgrade or gate any protocol request.

## Verification boundary

Shared evidence uses the unchanged 1,044-case suite at language-repository
revision `aebaa9949fcdc8fa4fbe9abfdb175808f6982186`, the exact merge revision
executed by failing workflow 40582524750. The adapter's declared language contract
pin remains `6402e668ae85a1e7efe94b3ac3d51f233b730ca4`: this newer-suite result is
current-main compatibility, not a claim that its declared semantic pin changed.
Capability declarations, shared expected results, comparator, normalizer and
known-host-gap metadata are unchanged.

Verified shared result: **1,044 total / 293 PASS / 0 FAIL / 751 UNSUPPORTED**;
protocol errors, crashes, timeouts and invalid cases are all zero. The unchanged
local host-parity gate reports **OK** against fresh Python protocol evidence.
All nine original failures pass; 14 additional cases execute and pass
through the repaired paths, with no previously passing case becoming unsupported.

Regression provenance and per-case diagnosis are recorded in
`.genia/process/issue-53-57-preflight.md` and
`.genia/process/issue-53-57-evidence.md`.
Independent maintainability and behavioral/truth review are required before
integration or closing the issues.


## Final #53/#54 acceptance follow-up

The maintainer explicitly authorized the bounded guard extension and accepted
nested lambda-arm header capture as the nested-arm requirement. The positive
case `f(x) = (n) -> (m) -> m + x; g = f(3); g(4)` returns `7` in both hosts.
Doubly nested list-pattern capture also returns `6`. Literal nested multi-arm
case syntax rejected by Python is not introduced by this follow-up.

Ordinary case-arm guards use the positional header frame and arm-local bindings.
Local explicit-R20 guards use pattern bindings; grouped explicit-open headers
remain nonbindings and free capture in guards or results remains refused.
Guards execute once, after successful matching, in source order. Errors stop
selection. Duplicate keys include guard structure, pattern binder slots, typed
literals and lambda scopes; alpha-equivalent guards duplicate, differing guards
remain separate clauses. The portable existing `IrCaseClause.guard` field is
present only when supplied. No capability, oracle, normalizer, semantic contract
pin or protocol version changes. Unsupported forms remain unsupported.

The separate RED commit is `166fc5b8c1654f529838b9f3307dad0892affd31`:
four missing positive guard checks fail and six supported controls pass. Native
coverage also exercises guard order/errors, shadowing, reference truthiness,
guard-key lambda shadowing/capture, integer spelling and omitted IR fields.
The two former negative guard-refusal assertions are replaced with demonstrated
positive outputs, not weakened to accept unsupported results. Existing nesting
and explicit-open header-capture controls are retained.

Acceptance matrix and explicit maintainer decisions are in
`.genia/process/issue-53-54-acceptance-preflight.md`. The prior repair's blanket
guard exclusion is superseded only for this authorized bounded follow-up.
Current exact-head shared/native/CI and independent-review evidence is recorded
in the PR handoff; issue closure and authority pin integration still require
those gates. Earlier suite counts above remain historical evidence.
