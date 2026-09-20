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
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"

namespace genia::protocol {

using json = nlohmann::json;

inline constexpr const char* kProtocolVersion = "1";

// The pinned genia-2026 contract revision this adapter was built against.
// Update deliberately (see README.md's "Pinned contract" table) as this
// repository advances -- never silently, matching the same discipline the
// Python bootstrap stub this replaces documented.
inline constexpr const char* kContractRevision = "41e5d80b79f1b05d6836a489ff70ba4f751e67e9";

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
    "genia-cpp is toolchain-bootstrap only (E24-1, "
    "m0smith/genia-2026#955); no Genia parsing, lowering, or evaluation "
    "is implemented yet -- see https://github.com/m0smith/genia-cpp "
    "AGENTS.md";

// Builds the `capabilities` operation's `ok` response: every known
// capability declared `unsupported`, an empty `operations` list (this
// adapter performs none of parse/lower/eval/cli), and the pinned
// contract/protocol version.
inline json build_capabilities_response(const std::string& case_id) {
  json capabilities = json::object();
  for (const auto& name : known_capabilities()) {
    capabilities[name] = "unsupported";
  }
  json result = {
      {"capabilities", capabilities},
      {"operations", json::array()},
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
