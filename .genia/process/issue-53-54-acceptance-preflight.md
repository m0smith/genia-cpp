# Final Slice C acceptance evidence: #53 and #54

Owner: Codex; maintainer/integration owner m0smith. User authorizes an acceptance
matrix, positive native tests, separate RED/correction commits if required,
minimal approved C++ corrections, CI and independent review. No merge or authority
pin update before review gates; no #1270 main merge or R29.

Read genia-2026 AGENTS first and refreshed main at
25a9c07013d3ed1169f4b707b7756044256e0a58; candidate #1270 remains
d55524dff62c036951301143781e62eb5310ef68. Read STATE, RULES, REPL README,
README, R20 contract sections 2.2/3.3/6/8, Model B contract 2.2/2.3, C++ AGENTS,
host boundaries, killer workflow, roadmap, run-change and complete #1251 audit.
C++ baseline main is 5c2d6a5bede00befa0b34b83df13364bd21babfb; reviewed
implementation 1af65fd2425526573f9ca765b3a027e37dfa5c74 is already contained.

The original nine portability failures are repaired. Do not repeat those fixes.
Existing C++ preflight explicitly requires guards remain unsupported; README and
parser exclude them. The new task explicitly requires a maintainer decision if
an acceptance case exceeds that boundary. Guard implementation is therefore
pending that decision, not silently authorized by the issue text. Native RED
tests may record the missing behavior before production correction.

## Criterion-to-test matrix

Categories: (1) implemented/demonstrated; (2) implemented/missing positive test;
(3) unsupported bounded host; (4) genuine defect in an approved supported path;
(5) explicit maintainer reconciliation required. No new category-4 defect is
established by the current probes; categories 3/5 are not PASS.

| Issue / criterion | Existing/new evidence | Disposition |
| --- | --- | --- |
| #53 ordinary grouped header captures | existing check_repair_1 and original shared case; [0,10] | 1 |
| #53 nested list-pattern capture | new doubly nested pattern probe returns 6 | 2, add positive native regression |
| #53 nested lambda arm captures header | f(x)=(n)->(m)->m+x; g=f(3); g(4) returns 7 in both hosts | 2, add positive native regression; 5 for acceptance interpretation |
| #53 literal nested multi-arm case in an arm block | attempted spelling rejected by Python too | 5; no invented C++ syntax/oracle; not a valid positive reference |
| #53 positive ordinary guard, including header reference | Python [6,0]; C++ unsupported | 3/5; native RED; await bounded guard authorization |
| #53 explicit-open free header capture remains rejected | existing header-capture checks; C++ unsupported and Python parse diagnostic | 1 for refusal boundary, not exact diagnostic parity |
| #53 original hdr/d8 cases no new failure | run unchanged full shared suites at main and candidate | final evidence required |
| #54 alias A | existing check_repair_2, exact stdout/stderr/exit matches Python | 1 |
| #54 flattened duplicate B and redeclaration C | existing check_repair_3, exact duplicate provenance/error | 1 |
| #54 guards differ | Python [1,2]; C++ unsupported | 3/5; native RED; no unsupported-as-PASS |
| #54 alpha-renamed equal guards remain duplicates | Python exact duplicate diagnostic; C++ unsupported | 3/5; native RED protects guard-key correction |
| both RED before fix | original ffb9b80 repair RED retained; new distinct RED for outstanding guards | no production fix before scope decision |
| both zero failure classes, unchanged capabilities/oracles | fresh baseline suites, native controls, checker/CI | pending exact final approved implementation |
| both independent maintainability/truth review | request exact-head Codex review after publication | pending; no self-approval |

## Scope and portability

Existing authority defines guard semantics; no new language contract is invented.
Possible bounded implementation: grouped ordinary arm guards; local explicit-R20
guards; existing portable guard fields; once-per-attempt evaluation using proper
header/arm scope; typed structural guard keys alpha-normalized with pattern
binders; preserve explicit-open header-capture refusal. Keep default-open headers,
varargs, module linking, new capabilities, protocol/pin changes and oracles out.
Do not implement literal nested case spelling without a valid reference and
explicit authority clarification. Preserve nesting limits and error boundaries.

Seven portability fields: authority STATE/R20; representation existing Core IR;
evidence native/unchanged shared cases; applicability existing gates; claims only
verified C++ subset; gaps no new waiver/reclassification; truth synchronization
native acceptance documentation, authority host status only after verified work.
MCP knowledge impact: no new language fact; a possible C++ boundary extension
does not change Python MCP semantics. Guard scope decision precedes production
work and any truthful implementation-status synchronization.

Strategy: reliable existing pipeline helpers; no optimizer, R29 or new runtime
framework. Native RED/positive controls, full shared corpus and zero failure
taxonomy, existing R20/depth controls, format/tidy and canonical code docs are
the checks. Record the current authority and candidate separately; compatibility
at an ancestor-declared C++ contract is not exact-current contract certification.
