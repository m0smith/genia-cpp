// E24-5 (m0smith/genia-2026#959) diagnostic-normalization boundary
// adversarial audit: internal-only tests (not shared genia-2026
// conformance evidence -- the issue's own "Affected docs/tests/specs:
// genia-cpp test suite only") proving that malformed, resource-limit-
// triggering, and internally-exceptional input at every layer (parser,
// lowering, evaluator, native primitives, and the E16-1 adapter
// boundary) never produces forbidden text (pre-flight section 5: STL
// exception wording, compiler/runtime-specific type names, OS/library
// error strings, filesystem/library implementation details, demangled
// C++ symbols) and -- just as important, since a crash is a far worse
// failure than any text leak -- never crashes the process.
//
// Two crashes were found and fixed by this ticket's own preparation
// (verified empirically against a build without the fixes in
// evaluator.hpp/parser.hpp): a C++ stack overflow (undefined behavior,
// uncatchable by any try/catch) from a few thousand levels of Genia-
// level function-call recursion, and the same from a few thousand
// levels of parser nesting (parenthesized grouping, list/map literals,
// list/map patterns). Both are hardened with a depth guard well below
// the measured crash threshold; the tests below assert the guard fires
// (an honest `std::nullopt`/"unsupported"), not that the crash
// reproduces.
#include <sstream>
#include <string>

#include "../src/adapter.hpp"
#include "../src/engine.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_lower;
using genia::engine::try_parse;
using genia::engine::try_run;

namespace {

std::string nested_parens(int depth) {
  return std::string(static_cast<size_t>(depth), '(') + "1" +
         std::string(static_cast<size_t>(depth), ')');
}

std::string nested_lists(int depth) {
  return std::string(static_cast<size_t>(depth), '[') + "1" +
         std::string(static_cast<size_t>(depth), ']');
}

std::string recursive_sum_list_source(int list_length) {
  std::ostringstream items;
  for (int i = 0; i < list_length; ++i) {
    if (i > 0) {
      items << ", ";
    }
    items << "1";
  }
  return "sum_list(xs) = [] -> 0 | [x, ..rest] -> x + sum_list(rest)\nsum_list([" + items.str() +
         "])\n";
}

}  // namespace

TEST_CASE(
    "hardening: several thousand levels of parenthesized grouping is unsupported, never a crash") {
  CHECK_FALSE(try_parse(nested_parens(5000)).has_value());
  CHECK_FALSE(try_run(nested_parens(5000)).has_value());
}

TEST_CASE(
    "hardening: several thousand levels of nested list literals is unsupported, never a crash") {
  CHECK_FALSE(try_parse(nested_lists(5000)).has_value());
  CHECK_FALSE(try_run(nested_lists(5000)).has_value());
}

TEST_CASE("hardening: a moderate nesting depth well under the guard still works normally") {
  // The guard must not fire for ordinary, shallow programs -- a
  // regression here would mean the threshold was set too low.
  auto result = try_run(nested_parens(50));
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "1\n");
}

TEST_CASE(
    "hardening: several thousand levels of recursive user-function calls is unsupported, never a "
    "crash") {
  // A real, valid, non-adversarial recursive Genia program (the same
  // shape as global_env.hpp's own map_acc) -- only its size is
  // adversarial. Segfaults a build without evaluator.hpp's call-depth
  // guard well before reaching this size.
  CHECK_FALSE(try_run(recursive_sum_list_source(5000)).has_value());
}

TEST_CASE(
    "hardening: recursion comfortably under the call-depth guard still produces the right answer") {
  auto result = try_run(recursive_sum_list_source(100));
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "100\n");
}

TEST_CASE("hardening: a very large integer literal is handled without crashing or hanging") {
  const std::string huge_digits(100000, '9');
  auto result = try_run(huge_digits + " + 1");
  REQUIRE(result.has_value());
  // 999...9 (100000 nines) + 1 = 1000...0 (a leading 1 followed by
  // 100000 zeros).
  CHECK(result->stdout_text.size() == 100002);  // digits + trailing '\n'
  CHECK(result->stdout_text.front() == '1');
  CHECK(result->stdout_text[1] == '0');
}

TEST_CASE("adapter boundary: deeply nested raw JSON input never crashes the request classifier") {
  const std::string deeply_nested_json(100000, '[');
  auto response = genia::adapter::handle_request(deeply_nested_json + std::string(100000, ']'));
  // Not a well-formed request object either way; the important
  // assertion is that this call returns at all (no crash) and never
  // fabricates a response for input this malformed.
  CHECK_FALSE(response.has_value());
}

TEST_CASE(
    "adapter boundary: eval/cli/parse/lower with wildly wrong-typed fields stay unsupported, never "
    "crash") {
  using genia::protocol::json;
  const json wrong_types[] = {
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "eval"},
           {"input", {{"source", 12345}}}},
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "eval"},
           {"input", {{"source", json::array({1, 2, 3})}}}},
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "cli"},
           {"input", {{"argv", "not-an-array"}}}},
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "cli"},
           {"input", {{"argv", json::array({1, 2, 3})}}}},
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "parse"},
           {"input", {{"source", nullptr}}}},
      json{{"protocol_version", "1"},
           {"case_id", "x"},
           {"operation", "lower"},
           {"input", json::array({1, 2})}},
  };
  for (const auto& request : wrong_types) {
    auto response = genia::adapter::handle_request(request.dump());
    REQUIRE(response.has_value());
    CHECK((*response)["status"] == "unsupported");
    CHECK((*response)["result"].is_null());
    // The forbidden-text list (pre-flight section 5) is about internal
    // implementation detail leaking, not about the reason string
    // existing at all -- this adapter's one static, hand-written
    // kUnsupportedReason is exactly the same fixed text on every
    // unsupported response, so asserting it here also catches any
    // future change that starts interpolating raw exception text into
    // it.
    CHECK((*response)["unsupported_reason"] == genia::protocol::kUnsupportedReason);
  }
}

TEST_CASE(
    "adapter boundary: cli file mode with an unreadable/missing path stays unsupported, never "
    "crashes") {
  using genia::protocol::json;
  const json request = {
      {"protocol_version", "1"},
      {"case_id", "x"},
      {"operation", "cli"},
      {"input",
       {{"argv", json::array({"/definitely/does/not/exist/genia-program.genia"})},
        {"stdin", nullptr}}},
  };
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "unsupported");
}

TEST_CASE(
    "hardening: a case-dispatch function with pathologically many non-matching clauses stays "
    "unsupported") {
  // No pinned evidence needs a "no match" diagnostic (E24-4's
  // invoke_closure comment); this just confirms failing to match many
  // clauses in a row is still an ordinary, bounded std::nullopt, not a
  // crash or a runaway loop.
  std::ostringstream clauses;
  for (int i = 0; i < 500; ++i) {
    if (i > 0) {
      clauses << " | ";
    }
    clauses << "{k" << i << "} -> " << i;
  }
  auto result = try_run("f(x) = " + clauses.str() + "\nf(42)\n");
  CHECK_FALSE(result.has_value());
}
