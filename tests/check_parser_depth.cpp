// Native parser-depth regressions for PR #58's published NestingGuard repair.
// Plain C++ allows documentation inspection despite the existing Catch2 macro
// parser limitation. Failed checks write to stderr and return exit status 1.
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/adapter.hpp"
#include "../src/engine.hpp"

namespace {
using genia::engine::try_parse;
using genia::engine::try_run;
int failures = 0;
// Records a failed invariant without aborting the remaining checks.
void expect(bool condition) {
  if (!condition) {
    ++failures;
    std::cerr << "Parser-depth invariant failed\n";
  }
}
// Records the negated invariant, matching the original Catch2 CHECK_FALSE.
void expect_false(bool condition) { expect(!condition); }
// Stops only this check if an optional-result prerequisite is absent.
void require(bool condition) {
  expect(condition);
  if (!condition) throw std::runtime_error("missing prerequisite");
}
// Generates `depth` ordinary declaration wrappers around `innermost`:
// depth 2 gives `f() = { f() = { 1 } }`. This forces recursive block parsing
// before nested declarations are rejected, exposing the stack-safety boundary
// without a hand-written enormous fixture. Callers supply nonnegative depth
// and valid innermost source. Returns an owned string without evaluation or IO;
// string allocation may throw.
std::string nested_function_blocks(int depth, const std::string& innermost = "1") {
  std::string source;
  for (int i = 0; i < depth; ++i) {
    source += "f() = { ";
  }
  source += innermost;
  for (int i = 0; i < depth; ++i) {
    source += " }";
  }
  return source;
}

// adapter boundary: 2000 nested declaration blocks return a controlled response
void check_deep_adapter_response() {
  using genia::protocol::json;
  const json request = {{"protocol_version", "1"},
                        {"case_id", "nested-function-block-depth"},
                        {"operation", "parse"},
                        {"input", {{"source", nested_function_blocks(2000)}}}};

  auto response = genia::adapter::handle_request(request.dump());
  require(response.has_value());
  expect((*response)["case_id"] == "nested-function-block-depth");
  expect((*response)["status"] == "unsupported");
  expect((*response)["result"].is_null());
  expect((*response)["unsupported_reason"] == genia::protocol::kUnsupportedReason);
}

// hardening: function blocks retain ordinary expressions near the nesting boundary
void check_near_boundary() {
  const std::string source =
      "f() = { " + (std::string(250, '(') + "1" + std::string(250, ')')) + " }\nf()";
  auto result = try_run(source);
  require(result.has_value());
  expect(result->stdout_text == "1\n");
}

// hardening: shallow ordinary function blocks retain their existing semantics
void check_shallow_block() {
  auto result = try_run("f() = { x = 40; x + 2 }\nf()");
  require(result.has_value());
  expect(result->stdout_text == "42\n");
}

// hardening: nested open declarations remain unsupported without a crash
void check_nested_open() {
  expect_false(try_parse("f() = { open g(x) = x }").has_value());
  expect_false(try_parse(nested_function_blocks(2000, "open g(x) = x")).has_value());
}

}  // namespace

// Runs all four preserved depth regressions; any failed check or crash fails CTest.
int main() {
  try {
    check_deep_adapter_response();
  } catch (const std::runtime_error&) {
  }
  try {
    check_near_boundary();
  } catch (const std::runtime_error&) {
  }
  try {
    check_shallow_block();
  } catch (const std::runtime_error&) {
  }
  try {
    check_nested_open();
  } catch (const std::runtime_error&) {
  }
  return failures == 0 ? 0 : 1;
}
