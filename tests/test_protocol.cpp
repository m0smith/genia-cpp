// Unit tests for the E24-1 E16-1 adapter skeleton.
//
// These are internal genia-cpp tests, not genia-2026 shared conformance
// evidence. Real conformance evidence for this ticket comes from running
// genia-2026's tools.spec_runner --host against the built binary (see
// README.md "Running the adapter" and m0smith/genia-2026#955's
// acceptance criteria).
#define CATCH_CONFIG_MAIN
#include <filesystem>
#include <fstream>

#include "../src/adapter.hpp"
#include "../src/genia2026_known_globals.hpp"
#include "../src/protocol.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::protocol::json;

// Separate the captureless test bodies from Catch2 registration so the
// documentation checker can parse the C++ declarations.

static const auto capabilities_response = [] {
  auto response = genia::adapter::handle_request(
      R"({"protocol_version":"1","case_id":"__capabilities__","operation":"capabilities","input":{}})");
  REQUIRE(response.has_value());
  const json& envelope = *response;

  CHECK(envelope["protocol_version"] == "1");
  CHECK(envelope["case_id"] == "__capabilities__");
  CHECK(envelope["operation"] == "capabilities");
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["unsupported_reason"].is_null());

  const json& result = envelope["result"];
  REQUIRE(result.contains("capabilities"));
  REQUIRE(result.contains("operations"));
  REQUIRE(result.contains("contract_revision"));
  REQUIRE(result.contains("protocol_version"));

  CHECK(result["operations"] == json::array({"parse", "lower", "eval", "cli"}));
  CHECK(result["protocol_version"] == "1");
  CHECK(result["contract_revision"].get<std::string>().size() > 0);

  const json& capabilities = result["capabilities"];
  REQUIRE(capabilities.contains("sheet_values"));
  CHECK(capabilities["sheet_values"] == "unsupported");
  CHECK_FALSE(capabilities.contains("sheet_relational"));
  CHECK(capabilities.size() == genia::protocol::known_capabilities().size());

  // As of E24-2, exactly the overridden capabilities are
  // supported/partial; every other known capability remains
  // unsupported (never claimed ahead of evidence).
  for (const auto& [name, status] : capabilities.items()) {
    bool is_override = false;
    for (const auto& [override_name, override_status] : genia::protocol::capability_overrides()) {
      if (override_name == name) {
        CHECK(status == override_status);
        is_override = true;
        break;
      }
    }
    if (!is_override) {
      CHECK(status == "unsupported");
    }
  }

  // Every declared capability name must come from the pinned genia-2026
  // vocabulary, never be invented locally.
  for (const auto& name : genia::protocol::known_capabilities()) {
    CHECK(capabilities.contains(name));
  }
};
REGISTER_TEST_CASE(capabilities_response,
                   "capabilities response matches this build's actual, evidence-backed support");

static const auto ci_contract_pin = [] {
  const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
  std::ifstream workflow(root / ".github/workflows/ci.yml");
  REQUIRE(workflow.is_open());

  std::string line;
  while (std::getline(workflow, line)) {
    if (line == "          repository: m0smith/genia-2026") {
      REQUIRE(static_cast<bool>(std::getline(workflow, line)));
      CHECK(line == "          ref: " + std::string(genia::protocol::kContractRevision));
      return;
    }
  }
  FAIL("Missing genia-2026 checkout in the CI workflow");
};
REGISTER_TEST_CASE(ci_contract_pin,
                   "the C++ contract revision matches the shared spec checkout in CI");

static const auto known_sheet_records = [] {
  CHECK(genia::genia2026_known_globals::is_known("sheet_records"));
};
REGISTER_TEST_CASE(known_sheet_records, "sheet_records is a known reference-host global");

static const auto empty_inputs = [] {
  for (const std::string operation : {"parse", "lower", "eval", "cli"}) {
    json request = {
        {"protocol_version", "1"},
        {"case_id", "some-case"},
        {"operation", operation},
        {"input", json::object()},
    };
    auto response = genia::adapter::handle_request(request.dump());
    REQUIRE(response.has_value());
    const json& envelope = *response;
    CHECK(envelope["case_id"] == "some-case");
    CHECK(envelope["operation"] == operation);
    CHECK(envelope["status"] == "unsupported");
    CHECK(envelope["result"].is_null());
    CHECK(envelope["unsupported_reason"].is_string());
    CHECK_FALSE(envelope["unsupported_reason"].get<std::string>().empty());
  }
};
REGISTER_TEST_CASE(empty_inputs, "parse/lower/eval/cli with no input fields are all unsupported");

static const auto arithmetic_eval = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "arithmetic-basic"},
      {"operation", "eval"},
      {"input", {{"source", "40 + 2\n"}, {"stdin", nullptr}, {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["result"]["stdout"] == "42\n");
  CHECK(envelope["result"]["stderr"] == "");
  CHECK(envelope["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(arithmetic_eval,
                   "end to end: eval of arithmetic-basic.yaml's source through the real adapter");

static const auto command_mode = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "command_mode_basic"},
      {"operation", "cli"},
      {"input", {{"argv", json::array({"-c", "print 123"})}, {"stdin", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  // The raw adapter response keeps the real trailing newline; the
  // cli-category expected value ("123", no newline) is compared after
  // tools/spec_runner's own trailing-newline stripping for this
  // category (see m0smith/genia-2026#965/#966) -- this adapter must
  // never strip it itself, or eval-category comparisons (which do NOT
  // strip) would be built on a different, undocumented convention.
  CHECK(envelope["result"]["stdout"] == "123\n");
  CHECK(envelope["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(command_mode,
                   "end to end: cli command_mode_basic.yaml's argv shape through the real adapter");

static const auto parse_integer = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "parse-literal-number"},
      {"operation", "parse"},
      {"input", {{"source", "42"}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["result"]["kind"] == "ok");
  CHECK(envelope["result"]["ast"]["kind"] == "Literal");
  CHECK(envelope["result"]["ast"]["value"] == 42);
};
REGISTER_TEST_CASE(parse_integer,
                   "end to end: parse of a bare integer literal through the real adapter");

static const auto unrenderable_lambda = [] {
  // As of E24-4, `(x) -> x` parses and evaluates fine (it is a real
  // lambda literal), but a Closure value has no rendered display form
  // (see render.hpp) -- the whole `eval` case is still honestly
  // unsupported, now because of rendering rather than grammar.
  json request = {
      {"protocol_version", "1"},
      {"case_id", "lambda-unrenderable-result"},
      {"operation", "eval"},
      {"input", {{"source", "(x) -> x"}, {"stdin", nullptr}, {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "unsupported");
  CHECK(envelope["result"].is_null());
};
REGISTER_TEST_CASE(unrenderable_lambda,
                   "end to end: a bare lambda as the final result is a real unsupported response");

static const auto map_items = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "map-items"},
      {"operation", "eval"},
      {"input",
       {{"source", "m = map_put(map_put(map_new(), \"a\", 1), \"b\", 2)\nmap_items(m)\n"},
        {"stdin", nullptr},
        {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["result"]["stdout"] == "[[\"a\", 1], [\"b\", 2]]\n");
  CHECK(envelope["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(
    map_items, "end to end: eval of a list-literal + map-function source through the real adapter");

static const auto pipeline_map_sum = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "pipeline-map-sum"},
      {"operation", "eval"},
      {"input",
       {{"source", "[1, 2, 3] |> map((x) -> x + 1) |> sum"},
        {"stdin", nullptr},
        {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["result"]["stdout"] == "9\n");
  CHECK(envelope["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(pipeline_map_sum,
                   "end to end: eval of pipeline-map-sum.yaml's source through the real adapter");

static const auto undefined_name = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "error-undefined-name"},
      {"operation", "eval"},
      {"input", {{"source", "undefined_name"}, {"stdin", nullptr}, {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "ok");
  CHECK(envelope["result"]["stdout"] == "");
  CHECK(envelope["result"]["stderr"] == "Error: Undefined name: undefined_name\n");
  CHECK(envelope["result"]["exit_code"] == 1);
};
REGISTER_TEST_CASE(undefined_name,
                   "end to end: error-undefined-name.yaml is a genuine ok result, not unsupported");

static const auto unknown_operation = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "future-op-case"},
      {"operation", "some_future_operation"},
      {"input", json::object()},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "unsupported");
  CHECK((*response)["operation"] == "some_future_operation");
};
REGISTER_TEST_CASE(unknown_operation,
                   "an unknown future operation is still deterministically unsupported");

static const auto repl_recovers = [] {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "repl-session"},
      {"operation", "cli"},
      {"input", {{"argv", json::array()}, {"stdin", "x = 1\nundefined_name\nx + 1\n"}}}};
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "ok");
  CHECK((*response)["result"]["stdout"] == "1\n2\n");
  CHECK((*response)["result"]["stderr"] == "Error: Undefined name: undefined_name\n");
  CHECK((*response)["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(repl_recovers,
                   "scripted REPL keeps bindings and recovers after a failed submission");

static const auto repl_none = [] {
  json request = {{"protocol_version", "1"},
                  {"case_id", "repl-none"},
                  {"operation", "cli"},
                  {"input", {{"argv", json::array()}, {"stdin", "none(\"nil\")\nx = (\n"}}}};
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "ok");
  CHECK((*response)["result"]["stdout"] == "none(\"nil\")\n");
  CHECK((*response)["result"]["exit_code"] == 0);
};
REGISTER_TEST_CASE(repl_none,
                   "scripted REPL renders none and drops an incomplete final submission");

static const auto repl_multiline = [] {
  json request = {{"protocol_version", "1"},
                  {"case_id", "repl-multiline"},
                  {"operation", "cli"},
                  {"input", {{"argv", json::array()}, {"stdin", "x = (\n1 + 2\n)\nx + 4\n"}}}};
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "ok");
  CHECK((*response)["result"]["stdout"] == "3\n7\n");
};
REGISTER_TEST_CASE(repl_multiline, "scripted REPL evaluates multiline source after completion");

static const auto malformed_json = [] {
  auto response = genia::adapter::handle_request("this is not json");
  CHECK_FALSE(response.has_value());
};
REGISTER_TEST_CASE(malformed_json,
                   "malformed JSON on stdin yields no response (never a fabricated envelope)");

static const auto json_array = [] {
  auto response = genia::adapter::handle_request("[1, 2, 3]");
  CHECK_FALSE(response.has_value());
};
REGISTER_TEST_CASE(json_array, "a JSON array instead of an object yields no response");

static const auto missing_case_id = [] {
  json request = {
      {"protocol_version", "1"}, {"operation", "capabilities"}, {"input", json::object()}};
  auto response = genia::adapter::handle_request(request.dump());
  CHECK_FALSE(response.has_value());
};
REGISTER_TEST_CASE(missing_case_id, "a request missing case_id yields no response");

static const auto missing_operation = [] {
  json request = {{"protocol_version", "1"}, {"case_id", "x"}, {"input", json::object()}};
  auto response = genia::adapter::handle_request(request.dump());
  CHECK_FALSE(response.has_value());
};
REGISTER_TEST_CASE(missing_operation, "a request missing operation yields no response");

static const auto response_encoding = [] {
  auto response = genia::adapter::handle_request(
      R"({"protocol_version":"1","case_id":"c1","operation":"eval","input":{}})");
  REQUIRE(response.has_value());
  const std::string encoded = genia::protocol::encode_response(*response);
  REQUIRE_FALSE(encoded.empty());
  CHECK(encoded.back() == '\n');
  // Exactly one trailing newline, no embedded newlines (single-line JSON).
  CHECK(encoded.find('\n') == encoded.size() - 1);
  json reparsed = json::parse(encoded);
  CHECK(reparsed["case_id"] == "c1");
};
REGISTER_TEST_CASE(
    response_encoding,
    "response envelopes serialize as exactly one JSON object with a trailing newline");
