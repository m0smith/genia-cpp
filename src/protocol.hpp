// E16-1 host-adapter wire protocol: request/response envelope helpers.
//
// Implements only the wire-level shapes defined in genia-2026's
// docs/design/r16-multi-host-conformance-infrastructure-contract.md and
// tools/spec_runner/protocol.py. This header knows nothing about Genia
// language semantics; it is the transport boundary only (see
// docs/design/r24/native-primitive-inventory.md's "E16-1 adapter
// transport" row for why this is native/host-local, not portable Genia
// behavior).
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"

namespace genia::protocol {

using json = nlohmann::json;

inline constexpr const char* kProtocolVersion = "1";

// The pinned genia-2026 contract revision this adapter was built against.
// Update deliberately (see README.md's "Pinned contract" table) as this
// repository advances -- never silently, matching the same discipline the
// Python bootstrap stub this replaces documented.
//
// Re-pinned for E24-2 (m0smith/genia-2026#956) to the revision after
// merging two evidence/tooling fixes that slice's own preparation found
// necessary: #964 (bootstrap-cases.json's "literals" category cited a
// case requiring out-of-scope pattern dispatch) and #966 (the generic
// external-host `cli`-category path never stripped trailing newlines,
// unlike the in-process Python-host path every spec/cli/*.yaml case's
// expected_stdout assumes). Re-pinned again for E24-3
// (m0smith/genia-2026#957) after merging #968/#969: three of the four
// pinned E24-3 bootstrap categories cited cases requiring E24-4/E24-7-
// scope pattern dispatch, recursion, or Decimal numbers, replaced with
// narrower cases proving the same already-approved R17/R18 behavior.
// Re-pinned again for E24-6 (m0smith/genia-2026#960) after merging
// #972 (the pinned open_functions_r20 bootstrap case required
// out-of-scope multi_file_eval, replaced with a single-file sibling
// case) and #974 (the roadmap's own guidance to declare
// open_functions `partial` was itself wrong -- `supported` is the
// correct declaration; see protocol.hpp's capability_overrides()).
inline constexpr const char* kContractRevision = "eb171afc434b3b9110007b8be76f6b0eff850311";

// Every capability name genia-2026's spec/manifest.json currently defines
// (required_capabilities + optional_capabilities), pinned at the contract
// revision above. This adapter declares every one of them "unsupported":
// E24-1 is toolchain bootstrap only and implements no Genia semantics.
// This list must never be extended locally -- a name must come from
// genia-2026's spec/manifest.json, updated deliberately alongside a
// contract-revision bump.
inline const std::vector<std::string>& known_capabilities() {
  static const std::vector<std::string> kNames = {
      // required_capabilities
      "parser",
      "ast_lowering",
      "core_ir_eval",
      "cli_file_mode",
      "cli_command_mode",
      "prelude_autoload",
      "doc_help",
      "shared_spec_runner",
      // optional_capabilities
      "cli_pipe_mode",
      "repl",
      "flow_phase_1",
      "http_server",
      "http_outbound_transport",
      "allowlisted_host_interop",
      "refs",
      "process_primitives",
      "bytes_json_zip",
      "debugger_stdio",
      "shell_stage",
      "resource_io",
      "configuration_environment_snapshot",
      "configuration_dotenv_snapshot",
      "model_deterministic_fixture",
      "model_gemini_rest",
      "embedding_deterministic_fixture",
      "indexing_deterministic_fixture",
      "retrieval_deterministic_fixture",
      "reranking_deterministic_fixture",
      "open_functions",
      "multi_file_eval",
  };
  return kNames;
}

inline constexpr const char* kUnsupportedReason =
    "genia-cpp implements only the E24-2..E24-7(increment 2) vertical "
    "slice (integer/Decimal literals, unary minus, string/boolean/list/map "
    "literals, bare-name references, assignment, `+ - * / == != %` binary "
    "expressions, lambdas, named-function definitions (ordinary and local "
    "case/pattern-dispatch bodies), local R20 open functions (single "
    "module only), pipelines, `err(...)` Outcomes, `rational(...)` "
    "construction, the R22 exact-family (Integer/Decimal/Rational) "
    "equality bridge, the one deterministic undefined-name runtime error, "
    "calls to this slice's native map_*/utf8_encode/err/sum functions, and "
    "`-c`/file-mode CLI); this request is outside that scope -- see "
    "https://github.com/m0smith/genia-cpp AGENTS.md";

// Per E24-2 (m0smith/genia-2026#956), E24-3 (m0smith/genia-2026#957), and
// E24-4 (m0smith/genia-2026#958): `parser`, `ast_lowering`,
// `cli_command_mode`, and `cli_file_mode` are declared `supported` (this
// slice's parser and lowering handle their entire grammar, and both CLI
// entry points are fully wired); `core_ir_eval` remains `partial` (E24-4
// added Outcome values, lambdas/closures, local case/pattern dispatch,
// pipelines, and one deterministic runtime-error diagnostic over this
// slice's minimal grammar, but Decimal/Rational/Float64, `some`/`none`,
// and general diagnostic normalization all remain unimplemented).
// E24-6 (m0smith/genia-2026#960) declares `open_functions` `supported`
// -- deliberately, not `partial`: `tools/spec_runner/capabilities.py`'s
// requires-gate only attempts a case whose `requires` list names
// `open_functions` when it is declared exactly `supported` (`partial`
// grants zero evidence credit; see `m0smith/genia-2026#973`/`#974`).
// This does not claim cross-module `extend`/`use`, the R20 diagnostic
// family (no-matching-case/duplicate-clause/varargs-ambiguity), or bare
// varargs patterns are implemented -- those remain genuinely
// `unsupported` per case (or are gated out entirely by every
// cross-module case's separate `multi_file_eval` requirement), exactly
// like `parser`/`ast_lowering` being `supported` has never meant every
// possible parse-category case passes. Every other capability remains
// `unsupported`. This map is the single source of truth for both the
// `capabilities` response and this project's own honesty: a name
// absent here defaults to `unsupported`.
inline const std::vector<std::pair<std::string, std::string>>& capability_overrides() {
  static const std::vector<std::pair<std::string, std::string>> kOverrides = {
      {"parser", "supported"},        {"ast_lowering", "supported"},
      {"core_ir_eval", "partial"},    {"cli_command_mode", "supported"},
      {"cli_file_mode", "supported"}, {"open_functions", "supported"},
  };
  return kOverrides;
}

// Builds the `capabilities` operation's `ok` response: every known
// capability declared per capability_overrides() (defaulting to
// `unsupported`), the operations this adapter actually attempts, and
// the pinned contract/protocol version.
inline json build_capabilities_response(const std::string& case_id) {
  json capabilities = json::object();
  for (const auto& name : known_capabilities()) {
    capabilities[name] = "unsupported";
  }
  for (const auto& [name, status] : capability_overrides()) {
    capabilities[name] = status;
  }
  json result = {
      {"capabilities", capabilities},
      {"operations", json::array({"parse", "lower", "eval", "cli"})},
      {"contract_revision", kContractRevision},
      {"protocol_version", kProtocolVersion},
  };
  return json{
      {"protocol_version", kProtocolVersion},
      {"case_id", case_id},
      {"operation", "capabilities"},
      {"status", "ok"},
      {"result", result},
      {"unsupported_reason", nullptr},
  };
}

// Builds the deterministic `unsupported` response every non-capabilities
// operation returns from this adapter.
inline json build_unsupported_response(const std::string& case_id, const std::string& operation) {
  return json{
      {"protocol_version", kProtocolVersion},
      {"case_id", case_id},
      {"operation", operation},
      {"status", "unsupported"},
      {"result", nullptr},
      {"unsupported_reason", kUnsupportedReason},
  };
}

// Serializes a response envelope as UTF-8 JSON plus a trailing newline,
// matching tools/spec_runner/protocol.py's encode_response exactly
// (sorted keys, one object, one trailing newline, nothing else on
// stdout).
inline std::string encode_response(const json& response) { return response.dump() + "\n"; }

}  // namespace genia::protocol
