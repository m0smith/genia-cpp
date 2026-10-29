// Native regressions for issues #53-#57, paired with the authoritative shared
// cases. Tests exercise real parsing, lowering, evaluation and scripted REPL;
// exit 1 reports failed invariants. No timing or network dependencies.
#include <iostream>
#include <stdexcept>
#include <string>

#include "../src/engine.hpp"

namespace {
using genia::engine::try_lower;
using genia::engine::try_parse;
// Native command-source observations supply a real filename for diagnostics.
std::optional<genia::engine::RunResult> run(const std::string& source) {
  return genia::engine::try_run(source, false, std::nullopt, "<command>");
}
int g_failures = 0;
std::string g_current_check;

// Records a false invariant in the active test without ending its checks.
void expect(bool condition) {
  if (!condition) {
    ++g_failures;
    std::cerr << "FAILED: " << g_current_check << "\n";
  }
}

// Stops a test whose prerequisite failed, so later dereferences stay safe.
void require(bool condition) {
  expect(condition);
  if (!condition) throw std::runtime_error("prerequisite failed");
}

// host repair #53: ordinary grouped bodies bind header before arm locals
void check_repair_1() {
  for (const auto& source :
       {"f(x) = (0) -> x | (n) -> n + x\n[f(0), f(5)]", "f(a) = (n) -> n + a\nf(3)",
        "f(a) = ([n]) -> n + unwrap_or(0, nth(0, a))\nf([3])"}) {
    auto result = run(source);
    require(result.has_value());
    expect(result->stderr_text.empty());
    expect(result->stdout_text ==
           (std::string(source).find("[f(") != std::string::npos ? "[0, 10]\n" : "6\n"));
  }
  expect(!(try_parse("open f(x) = (n) -> n + x\nf(3)").has_value()));
  auto guarded = run("f(x) = (n) ? n > 0 -> n + x\nf(3)");
  require(guarded.has_value());
  expect(guarded->stdout_text == "6\n");
}

// host repair #54: explicit open alias cannot be replaced
void check_repair_2() {
  auto result = run("open f(x) = x\na = f\na(x, y) = x + y\na(1, 2)");
  require(result.has_value());
  expect(result->stdout_text.empty());
  expect(result->exit_code == 1);
  expect(result->stderr_text ==
         "Error: Cannot define function a/2: name is bound to an open "
         "function interface or linked view, which an ordinary definition "
         "cannot replace\n");
}

// host repair #54: open duplicate keys normalize binders and retain clause lines
void check_repair_3() {
  for (const auto& source :
       {"open f(x) = (0) -> 1 | (n) -> 2\nf(0) = 3\nf(0)", "open f(x) = 1\nf(y) = 2\nf(3)",
        "open f([x, x]) = 1\nf([y, y]) = 2\nf([3, 3])"}) {
    auto result = run(source);
    require(result.has_value());
    expect(result->stdout_text.empty());
    expect(result->exit_code == 1);
    expect(result->stderr_text ==
           "Error: open-function-duplicate-clause: f in unit 'base' has "
           "two clauses with an identical dispatch key shape=('fixed', 1) "
           "(first at <command>:1, duplicate at <command>:2)\n");
  }
  auto distinct = run("open f([x, x]) = 1\nf([x, y]) = 2\nf([3, 4])");
  require(distinct.has_value());
  expect(distinct->stdout_text == "2\n");
  auto guarded = run("open f(x) ? x > 0 = 1\nf(y) ? y < 0 = 2\n[f(3), f(-3)]");
  require(guarded.has_value());
  expect(guarded->stdout_text == "[1, 2]\n");
}

// host repair #55: inline docs are metadata and string-only bodies stay bodies
void check_repair_4() {
  for (const auto& source : {"f(x) = \"d\" x + 1\nf(1)", "f(x) = \"d\" { x + 1 }\nf(1)",
                             "f(x) = \"d\" (0) -> x + 2 | (n) -> n + x\nf(1)",
                             "f(x) = \"d\" x + 1\nf(x, y) = \"d\" x + y\nf(1)"}) {
    auto result = run(source);
    require(result.has_value());
    expect(result->stdout_text == "2\n");
    expect(result->stderr_text.empty());
  }
  auto string_body = run("f(x) = \"body\"\nf(1)");
  require(string_body.has_value());
  expect(string_body->stdout_text == "\"body\"\n");
  auto parsed = genia::parser::parse_program("f(x) = \"d\" x + 1");
  require(parsed.has_value());
  expect(parsed->size() == 1);
  auto lowered = try_lower("f(x) = \"d\" x + 1");
  require(lowered.has_value());
  expect((*lowered)[0]["docstring"] == "d");
}

// host repair #55: conflicting docstrings reject before installation
void check_repair_5() {
  auto result = run("f(x) = \"d1\" x\nf(x, y) = \"d2\" x + y\nprint(99)");
  require(result.has_value());
  expect(result->stdout_text.empty());
  expect(result->exit_code == 1);
  expect(result->stderr_text ==
         "Error: Conflicting docstrings for function f: got 'd2', expected 'd1'\n");
}

// host repair #56: ordinary duplicates reject before subsequent effects
void check_repair_6() {
  for (const auto& source : {"f(x) = 1\nf(x) = 2\nprint(99)", "f(x) = 1\nf(y) = 2\nprint(99)",
                             "f(x) = (0) -> 1 | (n) -> 2\nf(y) = 3\nprint(99)"}) {
    auto result = run(source);
    require(result.has_value());
    expect(result->stdout_text.empty());
    expect(result->exit_code == 1);
    expect(result->stderr_text == "Error: Duplicate function definition: f/1\n");
  }
}

// host repair #56 #57: ordinary arities accumulate and aliases see later arities
void check_repair_7() {
  auto result = run("f(x) = x\na = f\nf(x, y) = x + y\n[f(4), a(1, 2)]");
  require(result.has_value());
  expect(result->stdout_text == "[4, 3]\n");
  auto repl = genia::engine::try_repl("f(x) = x; 0\na = f; 0\nf(x, y) = x + y; 0\na(1, 2)\nf(4)\n");
  require(repl.has_value());
  expect(repl->stdout_text == "0\n0\n0\n3\n4\n");
}

// host repair #57: complete semicolon submissions run, incomplete input stays inert
void check_repair_8() {
  auto result = genia::engine::try_repl("1; 2\nx = 1; 0\nx\nmissing; 8\nx\n");
  require(result.has_value());
  expect(result->stdout_text == "2\n0\n1\n1\n");
  expect(result->stderr_text == "Error: Undefined name: missing\n");
  auto incomplete = genia::engine::try_repl("x = (\n");
  require(incomplete.has_value());
  expect(incomplete->stdout_text.empty());
  expect(incomplete->stderr_text.empty());
  auto quoted = genia::engine::try_repl("\"a;b\"; \"c;d\"\n");
  require(quoted.has_value());
  expect(quoted->stdout_text == "\"c;d\"\n");
}

// host repair #57: REPL parses named block body without implicit main dispatch
void check_repair_9() {
  auto result = genia::engine::try_repl("main() = { print(\"ran\"); 1 }; 0\n42\n");
  require(result.has_value());
  expect(result->stdout_text == "0\n42\n");
  expect(result->stderr_text.empty());
}

// host repair #55: string expressions remain ordinary bodies
void check_repair_10() {
  auto result = run("f(x) = \"hello \" + x\nf(\"world\")");
  require(result.has_value());
  expect(result->stdout_text == "\"hello world\"\n");
}

}  // namespace

// Runs independent repair checks, continuing after failed prerequisites.
int main() {
  g_current_check = "check_repair_1";
  try {
    check_repair_1();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_2";
  try {
    check_repair_2();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_3";
  try {
    check_repair_3();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_4";
  try {
    check_repair_4();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_5";
  try {
    check_repair_5();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_6";
  try {
    check_repair_6();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_7";
  try {
    check_repair_7();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_8";
  try {
    check_repair_8();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_9";
  try {
    check_repair_9();
  } catch (const std::runtime_error&) {
  }
  g_current_check = "check_repair_10";
  try {
    check_repair_10();
  } catch (const std::runtime_error&) {
  }
  return g_failures == 0 ? 0 : 1;
}
