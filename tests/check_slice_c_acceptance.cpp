// Positive controls and outstanding RED requirements for issues #53/#54.
// Real engine execution preserves the Python-reference outputs/errors. Missing
// guard support is a failed acceptance requirement, never counted as a pass.
#include <iostream>
#include <string>

#include "../src/engine.hpp"

namespace {
int failures = 0;

// Compare one complete source program with its reference CLI observation.
// Unsupported source fails the named acceptance check without dereferencing it.
void check(const std::string& name, const std::string& source, const std::string& stdout_text,
           const std::string& stderr_text = "", int exit_code = 0) {
  auto result = genia::engine::try_run(source, false, std::nullopt, "<command>");
  if (!result.has_value()) {
    ++failures;
    std::cerr << "FAILED: " << name << " (unsupported; positive acceptance missing)\n";
    return;
  }
  if (result->stdout_text != stdout_text || result->stderr_text != stderr_text ||
      result->exit_code != exit_code) {
    ++failures;
    std::cerr << "FAILED: " << name << " (reference observation differs)\n";
    return;
  }
  std::cout << "PASS: " << name << "\n";
}
}  // namespace

// Exercise controls and required guard behavior independently; RED exits one.
int main() {
  check("ordinary header capture", "f(x) = (0) -> x | (n) -> n + x\n[f(0), f(5)]", "[0, 10]\n");
  check("nested pattern header capture",
        "f(a) = ([[n]]) -> n + unwrap_or(0, nth(0, unwrap_or([], nth(0, a))))\nf([[3]])", "6\n");
  check("nested lambda arm header capture", "f(x) = (n) -> (m) -> m + x\ng = f(3)\ng(4)", "7\n");
  check("ordinary arm guard", "f(x) = (n) ? n > 0 -> n + x | (n) -> x\n[f(3), f(0)]", "[6, 0]\n");
  check("ordinary guard captures header", "f(x) = (n) ? x > 0 -> n + x | (n) -> x\n[f(3), f(0)]",
        "[6, 0]\n");
  check("R20 alias rejection", "open f(x) = x\na = f\na(x, y) = x + y\na(1, 2)", "",
        "Error: Cannot define function a/2: name is bound to an open function interface or linked "
        "view, which an ordinary definition cannot replace\n",
        1);
  const std::string duplicate =
      "Error: open-function-duplicate-clause: f in unit 'base' has two clauses with an identical "
      "dispatch key shape=('fixed', 1) (first at <command>:1, duplicate at <command>:2)\n";
  check("R20 grouped duplicate", "open f(x) = (0) -> 1 | (n) -> 2\nf(0) = 3\nf(0)", "", duplicate,
        1);
  check("R20 ordinary redeclaration", "open f(x) = 1\nf(y) = 2\nf(3)", "", duplicate, 1);
  check("R20 guard differs", "open f(x) ? x > 0 = 1\nf(y) ? y < 0 = 2\n[f(3), f(-3)]", "[1, 2]\n");
  check("R20 alpha-equivalent guard duplicate", "open f(x) ? x > 0 = 1\nf(y) ? y > 0 = 2\nf(3)", "",
        duplicate, 1);
  return failures == 0 ? 0 : 1;
}
