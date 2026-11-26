# R31 CPP-1 contract re-pin and catalogue correction

CPP-1 pins the C++ adapter and its shared-spec CI checkout to
`b759c20f97334da14d1b15bdf8a70b87d6e0d2c8` of `m0smith/genia-2026`.
The adapter uses E16-1 protocol version 1. It reports `sheet_values` as
`unsupported`, and all 46 gated Sheet cases remain unsupported under
[m0smith/genia-2026#1330](https://github.com/m0smith/genia-2026/issues/1330).

The global catalogue distinguishes a reference-host binding that C++ cannot
evaluate from a genuinely undefined name. It now includes `sheet_records`.
Use the literal registration scan in `src/genia2026_known_globals.hpp` to
compare the catalogue against the pinned reference source. The scan also
identifies the unlisted internal `_execution_process` and the extra `test`
entry, which are outside CPP-1.

The shared-spec runner classifies this contract revision as `current`.
Of 1,187 shared cases, 335 pass and 852 are unsupported. Failures, protocol
errors, crashes, timeouts and invalid cases are all zero. See
[`evidence.json`](../evidence.json).
