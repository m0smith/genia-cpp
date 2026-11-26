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
  ok &= expect_eval("context and nonmatching value",
                    "f(none(r, c)) = c\nf(x) = 0\n"
                    "[f(none(\"x\", {key: 1})), f(none(\"x\")), f(1)]",
                    "[{key: 1}, none(\"nil\"), 0]\n");
  ok &= expect_eval("bare none pattern accepts any reason",
                    "f(none) = 1\nf(x) = 0\n[f(none(\"x\")), f(1)]", "[1, 0]\n");
  ok &= expect_eval("call-shaped pipeline propagates explicit none",
                    "f(a, b) = 5\n[1 |> f(none(\"a\"))]", "[none(\"a\")]\n");
  ok &= expect_eval("Flow map ordinary dispatch",
                    "step(x) = none(\"stop\")\n"
                    "evolve(1, step) |> take(2) |> map((x) -> 5) |> collect",
                    "[5, none(\"stop\")]\n");
  ok &= expect_eval("raw call's own arguments propagate",
                    "f(x) = 5\n[apply_raw(f, none(\"a\")), apply_raw(f, [none(\"a\")])]",
                    "[none(\"a\"), 5]\n");
  ok &= expect_eval("delegation to known aware function",
                    "probe(x) = unwrap_or(7, x)\n[probe(none(\"e\"))]", "[7]\n");
  ok &=
      expect_eval("err preserves an absence reason", "[err(none(\"x\"))]", "[err(none(\"x\"))]\n");
  ok &= expect_eval("constructor wrapper is ordinary", "f(x) = none(x)\n[f(none(\"x\"))]",
                    "[none(\"x\")]\n");
  ok &= expect_eval("absent context is raw null until returned",
                    "g(x) = 5\nf(none(r, c)) = [none?(c), c == c, g(c), c]\n"
                    "f(none(\"x\"))",
                    "[false, true, 5, none(\"nil\")]\n");
  ok &= expect_eval("Flow filter skips propagated none",
                    "step(x) = none(\"stop\")\n"
                    "evolve(1, step) |> take(2) |> filter((x) -> true) |> collect",
                    "[1]\n");
  ok &= request("eval", "f(x) = 5\na = f\na(none(r)) = r\na(1)")["status"] == "unsupported";
  ok &= expect_eval("standalone normalized nil has no auto-display",
                    "f(none(r, c)) = c\nf(none(\"x\"))", "");
  ok &= expect_eval("nested Flow run result propagates", "f(x) = 5\n[f(run(lines([\"a\"])))]",
                    "[none(\"nil\")]\n");
  return ok ? 0 : 1;
}
