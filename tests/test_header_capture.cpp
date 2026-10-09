// Unit tests for the R20 contract section 3.3 header-name capture check
// (src/header_capture.hpp, wired in src/parser.hpp). Behavior: a grouped open
// clause whose arm refers freely to a header name its own pattern does not
// bind is refused (unsupported), never run with an outer name captured;
// unambiguous programs keep their results. Evidence parallels the shared
// genia-2026 specs for issue #1067.
#include "../src/engine.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_parse;
using genia::engine::try_run;

// --- R20 contract section 3.3: grouped-clause header-name capture ---------
// genia-2026 issue #1067 (F1-D). The reference host rejects, before running
// anything, an arm that refers freely to a header name its pattern does not
// bind; this host has no parse-diagnostic channel, so it must refuse the
// program (unsupported) instead of silently capturing a global (`107`).

TEST_CASE("run: header-name capture of a global is unsupported, never a silent 107") {
  CHECK_FALSE(try_run("a = 7\n"
                      "open f(a, b) = (x, 0) -> a + 100 | (x, y) -> y\n"
                      "\n"
                      "f(1, 0)")
                  .has_value());
}

TEST_CASE("parse: header-name capture is unsupported at any expression position") {
  CHECK_FALSE(try_parse("open f(a, b) = (x, 0) -> a | (x, y) -> y").has_value());
  CHECK_FALSE(try_parse("open f(a, b) = (x, 0) -> [a, x] | (x, y) -> y").has_value());
  CHECK_FALSE(try_parse("open f(a, b) = (x, 0) -> a(x) | (x, y) -> y").has_value());
  CHECK_FALSE(
      try_parse("open f(a, b) = (x, 0) -> [x] |> map((v) -> v + a) | (x, y) -> [y]").has_value());
}

TEST_CASE("run: a header name the arm pattern rebinds is the arm's binder and still runs") {
  auto result = try_run(
      "open f(a, b) = (a, 0) -> a + 100 | (x, y) -> y\n"
      "\n"
      "f(1, 0)");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "101\n");
}

TEST_CASE("run: unreferenced header names, lambda shadowing, and outer names still run") {
  auto unreferenced = try_run(
      "open f(a, b) = (x, 0) -> x | (x, y) -> y\n"
      "\n"
      "f(5, 0)");
  REQUIRE(unreferenced.has_value());
  CHECK(unreferenced->stdout_text == "5\n");

  auto shadowed = try_run(
      "open f(a, b) = (x, 0) -> [x] |> map((a) -> a + 1) | (x, y) -> [y]\n"
      "\n"
      "f(5, 0)");
  REQUIRE(shadowed.has_value());
  CHECK(shadowed->stdout_text == "[6]\n");

  auto outer = try_run(
      "k = 7\n"
      "open f(a, b) = (x, 0) -> x + k | (x, y) -> y\n"
      "\n"
      "f(1, 0)");
  REQUIRE(outer.has_value());
  CHECK(outer->stdout_text == "8\n");
}
