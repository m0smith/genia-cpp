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

TEST_CASE("capabilities response declares every known capability unsupported") {
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

  CHECK(result["operations"].empty());
  CHECK(result["protocol_version"] == "1");
  CHECK(result["contract_revision"].get<std::string>().size() > 0);

  const json& capabilities = result["capabilities"];
  CHECK(capabilities.size() == genia::protocol::known_capabilities().size());
  for (const auto& [name, status] : capabilities.items()) {
    CHECK(status == "unsupported");
  }

  // Every declared capability name must come from the pinned genia-2026
  // vocabulary, never be invented locally.
  for (const auto& name : genia::protocol::known_capabilities()) {
    CHECK(capabilities.contains(name));
  }
}

TEST_CASE("parse/lower/eval/cli operations are all unsupported") {
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
