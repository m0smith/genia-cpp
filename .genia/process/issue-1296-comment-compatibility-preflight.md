# Documented shared-fixture comment compatibility pre-flight

Parent: genia-2026 #1269 / #1296, preparing #1270's integration and #1251 audit.
Branch: issue-1296-comment-compatibility; owner: Codex with maintainer integration.
The user explicitly authorized this focused follow-up, native regression coverage,
independent Codex sub-agent review with posted evidence, and a deliberate new
reviewed pin after the specified 78e61181 revision proved insufficient.

Authority: latest genia-2026 AGENTS, STATE, RULES, REPL README and README, the
existing shared CLI cases, R16 protocol, host porting guide and maintained-code
documentation policy. The shared fixture headers use existing Genia # comments;
no new language syntax, Core IR node, capability or semantics is proposed.
Python is the full-language reference; C++ stays a bounded Partial R27 host.

Scope: ignore # line comments outside strings, preserve newline/source locations,
and keep documentation-bearing ordinary main fixtures executable. Excludes @doc
annotation execution, doc_help, default-open functions, immutable bindings,
optimizer changes, Slice D/E, lifecycle changes, comparator/oracle/runner changes,
baseline/exclusion changes, main integration of #1270 and R29.

Portability analysis:
1. Authority/boundary: existing source grammar and E16 CLI/parse observations.
2. Representation: unchanged source tokens, AST/IR and normalized diagnostics.
3. Shared evidence: all 1,044 cases at the combined authority integration head;
   three existing main guards are unsupported with documentation comments and
   pass in diagnostic scratch copies without comments. Never edit their oracle.
4. Applicability: unchanged manifest/requires vocabulary and capability gates.
5. Claims: no declaration added, removed or upgraded; both hosts affected only
   through integration proof, with implementation confined to C++ tokenization.
6. Gaps: preserve existing issue-backed gaps; do not mask a failure as unsupported.
7. Truth: update the host's implemented boundary and source-provenance evidence;
   no MCP profile fact changes because no capability or language fact is added.

Test plan: separate native RED commit before the fix. Cover documented main/0,
main/1 preference and nonqualifying arity; trailing/EOF/CRLF comments, literal #
inside strings, comment line provenance in R20 duplicate diagnostics, and continued
annotation rejection. Run existing repair/depth targets, complete native tests,
full shared conformance, formatting, documentation ratchet and hosted CI.
Expected full-suite comparison: 293 PASS / 751 UNSUPPORTED with all actual failure
classes zero, restoring the three lost guards without changing other outcomes.

Examples: `# purpose\n1` and `x = 40 # note\nx + 2`; both existing portable source.
Complexity: one lexical branch, no new abstraction or runtime dependency. Risk:
low implementation complexity, medium drift risk from adopting a new host pin.
Preserves minimalism, explicit behavior and the validated-data-pipeline priority.
Contract/design phases reuse the existing language contract and lexical structure;
pre-flight -> failing native tests -> implementation/docs -> independent review
and validation -> separately checked authority pin integration. Keep both the
earlier baseline discovery-cleanup observation and SIGINT evidence truthful.
GO for this authorized bounded repair, 2026-10-28; no self-approval or gate bypass.
