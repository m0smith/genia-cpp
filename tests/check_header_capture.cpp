// Plain (non-Catch2) checks for the R20 contract section 3.3 header-name
// capture rule (src/header_capture.hpp, wired in src/parser.hpp).
//
// Behavior pinned: a grouped open clause whose arm refers freely to a header
// name its own pattern does not bind is refused (the engine reports
// unsupported) and never runs with an outer name captured; unambiguous
// programs keep their results. Parallels the shared genia-2026 specs for
// issue #1067. Exit status is 0 when every check holds, 1 otherwise; failures
// are named on stderr. Kept free of Catch2 test macros so the pinned
// documentation checker can parse the file.
#include <iostream>
#include <string>

#include "../src/engine.hpp"

namespace {

int g_failures = 0;

// Records a failed check by name; never throws.
void expect(bool condition, const std::string& name) {
  if (!condition) {
    ++g_failures;
    std::cerr << "FAILED: " << name << "\n";
  }
}

// True when the engine refuses `source` at parse time (unsupported).
bool parse_refused(const std::string& source) {
  return !genia::engine::try_parse(source).has_value();
}

// The stdout of running `source`, or "<unsupported>" when the engine refuses it.
std::string run_stdout(const std::string& source) {
  auto result = genia::engine::try_run(source);
  return result.has_value() ? result->stdout_text : "<unsupported>";
}

}  // namespace

// Runs every check and returns the process exit status.
int main() {
  expect(run_stdout("a = 7\n"
                    "open f(a, b) = (x, 0) -> a + 100 | (x, y) -> y\n"
                    "\n"
                    "f(1, 0)") == "<unsupported>",
         "a captured global is refused, never a silent 107");
  expect(parse_refused("open f(a, b) = (x, 0) -> a | (x, y) -> y"), "bare reference is refused");
  expect(parse_refused("open f(a, b) = (x, 0) -> [a, x] | (x, y) -> y"),
         "reference inside a list is refused");
  expect(parse_refused("open f(a, b) = (x, 0) -> a(x) | (x, y) -> y"),
         "reference in call position is refused");
  expect(parse_refused("open f(a, b) = (x, 0) -> [x] |> map((v) -> v + a) | (x, y) -> [y]"),
         "reference inside a lambda that does not rebind it is refused");

  expect(run_stdout("open f(a, b) = (a, 0) -> a + 100 | (x, y) -> y\n"
                    "\n"
                    "f(1, 0)") == "101\n",
         "a header name the arm pattern rebinds still runs");
  expect(run_stdout("open f(a, b) = (x, 0) -> x | (x, y) -> y\n"
                    "\n"
                    "f(5, 0)") == "5\n",
         "unreferenced header names still run");
  expect(run_stdout("open f(a, b) = (x, 0) -> [x] |> map((a) -> a + 1) | (x, y) -> [y]\n"
                    "\n"
                    "f(5, 0)") == "[6]\n",
         "a lambda parameter shadowing a header name still runs");
  expect(run_stdout("k = 7\n"
                    "open f(a, b) = (x, 0) -> x + k | (x, y) -> y\n"
                    "\n"
                    "f(1, 0)") == "8\n",
         "an outer name that is not a header name still runs");

  if (g_failures != 0) {
    std::cerr << g_failures << " header-capture check(s) failed\n";
    return 1;
  }
  return 0;
}
