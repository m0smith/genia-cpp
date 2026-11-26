// Exercise the portable absence patterns and ordinary invocation boundary.
// Named assertion failures go to stderr; the process returns nonzero on failure.
#include <iostream>

#include "../src/adapter.hpp"

namespace {
using genia::protocol::json;

// Run the requested adapter operation with an evaluated source fixture.
json request(const std::string& operation, const std::string& source) {
  const json input = {{"source", source}, {"argv", nullptr}, {"stdin", nullptr}};
  const json envelope = {{"protocol_version", "1"},
                         {"case_id", "none-dispatch"},
                         {"operation", operation},
                         {"input", input}};
  return genia::adapter::handle_request(envelope.dump()).value_or(json::object());
}

// Report a named mismatched result and keep running the remaining fixtures.
bool expect_eval(const std::string& name, const std::string& source, const std::string& output,
                 bool duplicate = false) {
  auto response = request("eval", source);
  if (response.value("status", "") == "ok") {
    const auto& result = response.at("result");
    if (duplicate && result.at("exit_code") == 1 &&
        result.at("stderr").get<std::string>().find("duplicate") != std::string::npos)
      return true;
    if (!duplicate && result.at("exit_code") == 0 && result.at("stdout") == output &&
        result.at("stderr") == "")
      return true;
  }
  std::cerr << "FAILED: " << name << " (" << response.value("status", "missing") << ")\n";
  return false;
}
}  // namespace

// Run the dispatch regressions without depending on environment or files.
int main() {
  bool ok = true;
  ok &= expect_eval("matching reason and ordinary fallback",
                    "choose(none(\"missing\")) = 1\nchoose(x) = 2\n"
                    "[choose(none(\"missing\")), choose(none(\"other\")), choose(err(\"e\"))]",
                    "[1, 2, 2]\n");
  ok &= expect_eval("reason bindings and live alias",
                    "pick(none(r)) = r\na = pick\npick(x) = 0\n[a(none(\"reason\")), a(42)]",
                    "[\"reason\", 0]\n");
  ok &= expect_eval("alpha-equivalent duplicate", "pick(none(a)) = a\npick(none(b)) = b\n0", "",
                    true);
  ok &= expect_eval("ordinary alias and first none",
                    "f(x, y) = 5\na = f\n[a(none(\"first\"), none(\"second\")), a(err(\"e\"), 0)]",
                    "[none(\"first\"), 5]\n");
  ok &= expect_eval("list pipeline", "f(x) = 5\n[1 |> f, none(\"a\") |> f]", "[5, none(\"a\")]\n");
  ok &= expect_eval("raw delivery", "f(x) = 5\n[apply_raw(f, [none(\"a\")])]", "[5]\n");
  return ok ? 0 : 1;
}
