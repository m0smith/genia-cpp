// E24-2 adapter request handling, factored out of main() so it is unit
// testable without spawning a subprocess (the E16-1 transport itself --
// stdin/stdout framing -- is exercised separately via the real
// genia-2026 spec_runner --host integration, per
// docs/design/r24/native-primitive-inventory.md's transport row).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "engine.hpp"
#include "protocol.hpp"

namespace genia::adapter {

using genia::protocol::json;

inline json build_ok_response(const std::string& case_id, const std::string& operation,
                              const json& result) {
  return json{
      {"protocol_version", genia::protocol::kProtocolVersion},
      {"case_id", case_id},
      {"operation", operation},
      {"status", "ok"},
      {"result", result},
      {"unsupported_reason", nullptr},
  };
}

// Handles `parse`: input {"source": "<string>"}.
inline std::optional<json> handle_parse(const std::string& case_id, const json& input) {
  if (!input.contains("source") || !input["source"].is_string()) {
    return std::nullopt;
  }
  auto ast = engine::try_parse(input["source"].get<std::string>());
  if (!ast.has_value()) {
    return std::nullopt;
  }
  return build_ok_response(case_id, "parse", json{{"kind", "ok"}, {"ast", *ast}});
}

// Handles `lower`: input {"source": "<string>"}.
inline std::optional<json> handle_lower(const std::string& case_id, const json& input) {
  if (!input.contains("source") || !input["source"].is_string()) {
    return std::nullopt;
  }
  auto ir = engine::try_lower(input["source"].get<std::string>());
  if (!ir.has_value()) {
    return std::nullopt;
  }
  return build_ok_response(case_id, "lower", json{{"ir", *ir}});
}

inline json run_result_to_json(const engine::RunResult& result) {
  return json{
      {"stdout", result.stdout_text},
      {"stderr", result.stderr_text},
      {"exit_code", result.exit_code},
  };
}

// Handles `eval`: input {"source": "<string>", "stdin": ..., "argv": ...}.
// This slice ignores stdin/argv: no pinned case exercises either.
inline std::optional<json> handle_eval(const std::string& case_id, const json& input) {
  if (!input.contains("source") || !input["source"].is_string()) {
    return std::nullopt;
  }
  auto result = engine::try_run(input["source"].get<std::string>());
  if (!result.has_value()) {
    return std::nullopt;
  }
  return build_ok_response(case_id, "eval", run_result_to_json(*result));
}

// Handles `cli`: input {"argv": [...], "stdin": ...}. Only the
// `-c <source>` command-mode shape is implemented at this slice (file
// mode is E24-3's scope; pipe/test modes are out of R24's floor
// entirely).
inline std::optional<json> handle_cli(const std::string& case_id, const json& input) {
  if (!input.contains("argv") || !input["argv"].is_array()) {
    return std::nullopt;
  }
  std::vector<std::string> argv;
  for (const auto& item : input["argv"]) {
    if (!item.is_string()) {
      return std::nullopt;
    }
    argv.push_back(item.get<std::string>());
  }
  if (argv.size() < 2 || argv[0] != "-c") {
    return std::nullopt;
  }
  const std::string& source = argv[1];
  auto result = engine::try_run(source);
  if (!result.has_value()) {
    return std::nullopt;
  }
  return build_ok_response(case_id, "cli", run_result_to_json(*result));
}

// Returns the response envelope for one request, or std::nullopt if the
// request cannot be classified (malformed JSON, not an object, or
// missing/mistyped case_id/operation) -- std::nullopt means: write
// nothing to stdout. This adapter never guesses a case_id/operation it
// was not given, and never self-reports protocol_error -- that
// classification is the runner's alone (R16 contract).
//
// For parse/lower/eval/cli specifically: std::nullopt from the
// operation handler above means "this case is outside what this slice
// implements" and is deliberately converted into a genuine
// `unsupported` protocol response here, never a crash or a guessed
// result.
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
  const json input = request.value("input", json::object());

  if (operation == "capabilities") {
    return genia::protocol::build_capabilities_response(case_id);
  }
  if (operation == "parse") {
    auto response = handle_parse(case_id, input);
    return response.has_value() ? response
                                : genia::protocol::build_unsupported_response(case_id, operation);
  }
  if (operation == "lower") {
    auto response = handle_lower(case_id, input);
    return response.has_value() ? response
                                : genia::protocol::build_unsupported_response(case_id, operation);
  }
  if (operation == "eval") {
    auto response = handle_eval(case_id, input);
    return response.has_value() ? response
                                : genia::protocol::build_unsupported_response(case_id, operation);
  }
  if (operation == "cli") {
    auto response = handle_cli(case_id, input);
    return response.has_value() ? response
                                : genia::protocol::build_unsupported_response(case_id, operation);
  }
  // Any operation this build predates: deterministically unsupported.
  return genia::protocol::build_unsupported_response(case_id, operation);
}

}  // namespace genia::adapter
