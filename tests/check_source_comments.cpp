// Native compatibility checks for documented shared source fixtures (#1269).
// Exercise real command-mode parsing/evaluation and normalized source locations.
// Exit 1 and named stderr failures expose violations; no timing or network IO.
#include <iostream>
#include <string>

#include "../src/adapter.hpp"

namespace {
int failures = 0;

// Records a named invariant without aborting the remaining checks.
void expect(bool condition, const std::string& name) {
  if (!condition) {
    ++failures;
    std::cerr << "FAILED: " << name << '\n';
  }
}

// Runs source through the real command-mode adapter with no stdin or arguments.
// Returns its optional protocol response; engine exceptions retain adapter policy.
std::optional<genia::protocol::json> command(const std::string& source) {
  using genia::protocol::json;
  const json request = {{"protocol_version", "1"},
                        {"case_id", "source-comments"},
                        {"operation", "cli"},
                        {"input", {{"argv", json::array({"-c", source})}, {"stdin", nullptr}}}};
  return genia::adapter::handle_request(request.dump());
}

// Requires a successful command envelope and its exact output; missing or
// unsupported responses fail without unsafe result dereferences.
void expect_output(const std::string& source, const std::string& output, const std::string& name) {
  auto response = command(source);
  if (!response || (*response)["status"] != "ok") {
    expect(false, name + ": command must be supported");
    return;
  }
  const auto& result = (*response)["result"];
  expect(result["stdout"] == output, name + ": stdout");
  expect(result["stderr"] == "", name + ": stderr");
  expect(result["exit_code"] == 0, name + ": exit code");
}

// The three affected shared main-selection shapes keep their existing outputs
// with module-purpose comments and inert inline named-function documentation.
void check_documented_main_guards() {
  expect_output(
      "# module purpose\nmain() = \"entry metadata\" { print(\"main0\"); 10 }\n"
      "print(\"top\")\n42",
      "top\nmain0\n10\n", "main/0 fallback");
  expect_output(
      "# module purpose\nmain(args) = \"entry metadata\" { print(\"main1\"); 11 }\n"
      "main() = { print(\"main0\"); 10 }\nprint(\"top\")\n42",
      "top\nmain1\n11\n", "main/1 preference");
  expect_output(
      "# module purpose\nmain(a, b) = \"entry metadata\" { print(\"main2\"); 12 }\n"
      "print(\"top\")\n42",
      "top\n42\n", "nonqualifying main arity");
}

// Comments terminate at LF or EOF, including CRLF input; # within a string is
// ordinary string data and never starts a comment.
void check_lexical_boundaries() {
  expect_output("x = 40 # trailing note\nx + 2 # EOF note", "42\n", "trailing and EOF comments");
  expect_output("# heading\r\n1 + 2 # suffix\r\n", "3\n", "CRLF comments");
  expect_output("\"# literal\" + \" # more\" # actual comment", "\"# literal # more\"\n",
                "hash characters inside strings");
}

// Skipping comment bytes must preserve original declaration lines in the
// existing R20 duplicate diagnostic, without changing its failure category.
void check_comment_line_provenance() {
  auto response = command("# heading\nopen f(x) = 1\n# between declarations\nf(y) = 2\nf(0)");
  if (!response || (*response)["status"] != "ok") {
    expect(false, "duplicate diagnostic remains supported");
    return;
  }
  const auto& result = (*response)["result"];
  expect(result["exit_code"] == 1, "duplicate diagnostic exit code");
  expect(result["stdout"] == "", "duplicate declaration has no later effects");
  expect(result["stderr"] ==
             "Error: open-function-duplicate-clause: f in unit 'base' has two clauses with an "
             "identical dispatch key shape=('fixed', 1) (first at <command>:2, duplicate at "
             "<command>:4)\n",
         "original declaration lines survive comments");
}

// Comment support does not imply annotation execution or the doc_help capability.
void check_annotations_stay_unsupported() {
  auto response = command("# heading\n@doc \"annotation\"\nf() = 1\nf()");
  expect(response && (*response)["status"] == "unsupported", "annotations remain unsupported");
}
}  // namespace

// Runs every lexical, CLI, diagnostic and unsupported-boundary check.
int main() {
  check_documented_main_guards();
  check_lexical_boundaries();
  check_comment_line_provenance();
  check_annotations_stay_unsupported();
  return failures == 0 ? 0 : 1;
}
