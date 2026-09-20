// Unit tests for the E24-1 E16-1 adapter skeleton.
//
// These are internal genia-cpp tests, not genia-2026 shared conformance
// evidence. Real conformance evidence for this ticket comes from running
// genia-2026's tools.spec_runner --host against the built binary (see
// README.md "Running the adapter" and m0smith/genia-2026#955's
// acceptance criteria).
#define CATCH_CONFIG_MAIN
#include "../src/adapter.hpp"
#include "../src/protocol.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::protocol::json;

TEST_CASE("capabilities response matches this build's actual, evidence-backed support") {
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
}

TEST_CASE("parse/lower/eval/cli with no input fields are all unsupported") {
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
}

TEST_CASE("end to end: eval of arithmetic-basic.yaml's source through the real adapter") {
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
}

TEST_CASE("end to end: cli command_mode_basic.yaml's argv shape through the real adapter") {
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
}

TEST_CASE("end to end: parse of a bare integer literal through the real adapter") {
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
}

TEST_CASE("end to end: unsupported-grammar source over eval is a real unsupported response") {
  json request = {
      {"protocol_version", "1"},
      {"case_id", "lambda-unsupported"},
      {"operation", "eval"},
      {"input", {{"source", "(x) -> x"}, {"stdin", nullptr}, {"argv", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  const json& envelope = *response;
  CHECK(envelope["status"] == "unsupported");
  CHECK(envelope["result"].is_null());
}

TEST_CASE("end to end: eval of a list-literal + map-function source through the real adapter") {
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
}

TEST_CASE("an unknown future operation is still deterministically unsupported") {
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
}

TEST_CASE("malformed JSON on stdin yields no response (never a fabricated envelope)") {
  auto response = genia::adapter::handle_request("this is not json");
  CHECK_FALSE(response.has_value());
}

TEST_CASE("a JSON array instead of an object yields no response") {
  auto response = genia::adapter::handle_request("[1, 2, 3]");
  CHECK_FALSE(response.has_value());
}

TEST_CASE("a request missing case_id yields no response") {
  json request = {
      {"protocol_version", "1"}, {"operation", "capabilities"}, {"input", json::object()}};
  auto response = genia::adapter::handle_request(request.dump());
  CHECK_FALSE(response.has_value());
}

TEST_CASE("a request missing operation yields no response") {
  json request = {{"protocol_version", "1"}, {"case_id", "x"}, {"input", json::object()}};
  auto response = genia::adapter::handle_request(request.dump());
  CHECK_FALSE(response.has_value());
}

TEST_CASE("response envelopes serialize as exactly one JSON object with a trailing newline") {
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
}
