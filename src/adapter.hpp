// E24-1 adapter request handling, factored out of main() so it is unit
// testable without spawning a subprocess (the E16-1 transport itself --
// stdin/stdout framing -- is exercised separately via the real
// genia-2026 spec_runner --host integration, per
// docs/design/r24/native-primitive-inventory.md's transport row).
#pragma once

#include <optional>
#include <string>

#include "../third_party/nlohmann_json/json.hpp"
#include "protocol.hpp"

namespace genia::adapter {

using genia::protocol::json;

// Returns the response envelope for one request, or std::nullopt if the
// request cannot be classified (malformed JSON, not an object, or
// missing/mistyped case_id/operation). std::nullopt means: write nothing
// to stdout. This adapter never guesses a case_id/operation it was not
// given, and never self-reports protocol_error -- that classification is
// the runner's alone (R16 contract).
inline std::optional<json> handle_request(const std::string& raw_stdin) {
  json request = json::parse(raw_stdin, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (request.is_discarded() || !request.is_object()) {
    return std::nullopt;
  }
  if (!request.contains("case_id") || !request["case_id"].is_string()) {
    return std::nullopt;
  }
  if (!request.contains("operation") || !request["operation"].is_string()) {
    return std::nullopt;
  }

  const std::string case_id = request["case_id"].get<std::string>();
  const std::string operation = request["operation"].get<std::string>();

  if (operation == "capabilities") {
    return genia::protocol::build_capabilities_response(case_id);
  }
  // parse, lower, eval, cli, or any operation this build predates: all
  // deterministically unsupported. E24-1 implements no Genia semantics.
  return genia::protocol::build_unsupported_response(case_id, operation);
}

}  // namespace genia::adapter
