# C++ host conformance repair pre-flight

Change: close genia-cpp #53–#57; branch issue-53-57-host-conformance.
Owner: Codex implementation, m0smith integration (current issue assignee).
Authorization: uploaded task explicitly requests RED, implementation, documentation,
and integration proposal; no semantic contract change or merge authorized.

Scope: the nine actual failures in run 40582524750. Excludes Python Slice C,
default_open_functions, immutable bindings, R29, cross-module open functions,
new capabilities, comparator changes and PR #1270 merge.

Authority: genia-2026 AGENTS, STATE function/documentation and R20 sections,
RULES 8.1 and R20, REPL contract R26 section 2, R20 contract 2.2/3.3/6,
Core IR portability, E16-1, shared cases at aebaa9949fcdc8fa4fbe9abfdb175808f6982186.
The request authorizes implementing the explicit maintainer semicolon ruling
R7-D4 (#57), with truth synchronization after verification. No new semantics.

Maturity: existing bounded Partial C++ host; supported capabilities unchanged.
Python already passes independently; no Python production changes required.
C++ currently loses grouped header bindings, parses docstrings as expressions,
replaces ordinary definitions, misses open duplicates/alias protection, and
rejects semicolons lexically then discards pending REPL input at EOF.

Portability analysis:
1. Observable boundary: source -> parser -> portable Core IR -> evaluator -> E16-1.
2. Authority/representation: existing function/REPL contracts; existing nodes only.
3. Shared evidence: nine cases mapped in the final report, no oracle edits.
4. Applicability: existing open_functions/repl requirements or ungated cases.
5. Host claims: no additions/downgrades; doc_help/default_open_functions unsupported.
6. Host gap: none for these repairs; no known_host_gaps changes.
7. Truth sync: C++ README/status; authority host/REPL docs only verified claims.

Test strategy: reproduce exact 1044-case baseline; commit focused C++ regressions
RED before correction; preserve R20 header-capture refusal and unsupported guards;
exercise declaration-time errors, metadata, alias identity, persistent arities,
REPL recovery/incompleteness; run all C++ tests, formatting, static analysis,
full shared suite and parity gate. Existing R22/R27 tests remain mandatory.
Examples: grouped f(x) = (0) -> x | (n) -> n + x; REPL 1; 2.
Classification: portable existing behavior; native tests implementation evidence.

Design: bind ordinary header names before arm-local bindings; keep explicit-open
identity distinct; validate alpha-normalized open patterns with original clause
lines; retain named ordinary overloads in the shared closure so aliases see new
arities; reject duplicate headers before mutation; carry optional docstring
metadata through AST/IR; tokenize semicolons as statement separators only.
No global registry, source substitution, protocol change or new runtime framework.
Guard syntax remains outside the grammar and must remain unsupported.

Complexity: reveals existing structure; bounded changes to parser, nodes,
lowering/projection, closure/evaluator and REPL. Drift risk medium.
MCP knowledge impact: none; no new language facts, profile or MCP capability.
Philosophy: preserves minimalism, explicit behavior, pattern matching and adapter
separation; portability repair supports reliable pipeline helper composition.

Phases: pre-flight -> distinct failing-test commit -> fix referencing RED SHA ->
documentation -> review/audit evidence. Existing contract/design reused; no new
contract phase. Independent review remains required before integration/closure.
GO for authorized repair, 2026-10-27; not a claim of independent review or merge GO.
