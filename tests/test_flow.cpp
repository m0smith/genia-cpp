// R27 E27-1 Flow phase 1 kernel tests. Expected values were checked against
// the Python reference host (`genia -c`) for each program; the shared
// `requires: [flow_phase_1]` specs in genia-2026 remain the conformance
// evidence, these pin kernel edge cases they do not expose.
#include <string>

#include "../src/adapter.hpp"
#include "../src/engine.hpp"
#include "../src/pipe_mode.hpp"
#include "../third_party/catch2/catch.hpp"

namespace {

using genia::engine::RunResult;

std::optional<RunResult> run_flow(const std::string& source, const std::string& stdin_text = "") {
  return genia::engine::try_run(source, false, stdin_text, "<command>");
}

std::string stdout_of(const std::string& source, const std::string& stdin_text = "") {
  auto result = run_flow(source, stdin_text);
  REQUIRE(result.has_value());
  REQUIRE(result->exit_code == 0);
  REQUIRE(result->stderr_text.empty());
  return result->stdout_text;
}

}  // namespace

TEST_CASE("Flow: stdin lines split on newline and keep empty lines") {
  CHECK(stdout_of("stdin |> lines |> collect", "a\nb\n") == "[\"a\", \"b\"]\n");
  CHECK(stdout_of("stdin |> lines |> collect", "a\n\nb") == "[\"a\", \"\", \"b\"]\n");
  CHECK(stdout_of("stdin |> lines |> collect", "\n") == "[\"\"]\n");
  CHECK(stdout_of("stdin |> lines |> collect", "") == "[]\n");
}

TEST_CASE("Flow: carriage returns in stdin are unsupported, never guessed") {
  CHECK_FALSE(run_flow("stdin |> lines |> collect", "a\r\nb\r\n").has_value());
}

TEST_CASE("Flow: take never over-pulls its source") {
  // evolve yields the seed first, so take(3) runs the step exactly twice.
  CHECK(stdout_of("evolve(0, (n) -> print(n + 1)) |> take(3) |> run") == "1\n2\n");
  CHECK(stdout_of("evolve(0, (n) -> print(n + 1)) |> take(0) |> run").empty());
}

TEST_CASE("Flow: take and drop clamp non-positive and oversized counts") {
  CHECK(stdout_of("stdin |> lines |> take(0) |> collect", "a\nb\n") == "[]\n");
  CHECK(stdout_of("stdin |> lines |> take(-1) |> collect", "a\nb\n") == "[]\n");
  CHECK(stdout_of("stdin |> lines |> take(5) |> collect", "a\nb\n") == "[\"a\", \"b\"]\n");
  CHECK(stdout_of("stdin |> lines |> drop(-1) |> collect", "a\nb\n") == "[\"a\", \"b\"]\n");
  CHECK(stdout_of("stdin |> lines |> drop(5) |> collect", "a\nb\n") == "[]\n");
}

TEST_CASE("Flow: two stdin Flows share one read position") {
  CHECK(stdout_of("a = stdin |> lines\nb = stdin |> lines\na |> take(1) |> collect\nb |> collect",
                  "1\n2\n3\n") == "[\"2\", \"3\"]\n");
}

TEST_CASE("Flow: a consumed Flow reports the single-use error through its stage") {
  auto result = run_flow("x = stdin |> lines\nx |> collect\nx |> collect", "a\n");
  REQUIRE(result.has_value());
  CHECK(result->exit_code == 1);
  CHECK(result->stdout_text.empty());
  CHECK(result->stderr_text ==
        "Error: pipeline stage 1 failed in Explicit bridge mode at collect [<command>:3]: "
        "stage received flow; Flow has already been consumed\n");
}

TEST_CASE("Flow: a downstream Flow consumes its upstream only when pulled") {
  // Building `y` from `x` does not consume `x`, so collecting `x` succeeds.
  CHECK(stdout_of("x = stdin |> lines\ny = x |> map(upper)\nx |> collect", "a\n") == "[\"a\"]\n");
  auto result =
      run_flow("x = stdin |> lines\ny = x |> map(upper)\nx |> collect\ny |> collect", "a\n");
  REQUIRE(result.has_value());
  CHECK(result->exit_code == 1);
  CHECK(result->stderr_text ==
        "Error: pipeline stage 1 failed in Explicit bridge mode at collect [<command>:4]: "
        "stage received flow; Flow has already been consumed\n");
}

TEST_CASE("Flow: single-use error outside a pipeline has no stage prefix") {
  auto result = run_flow("x = evolve(0, (n) -> n + 1) |> take(1)\ncollect(x)\ncollect(x)");
  REQUIRE(result.has_value());
  CHECK(result->exit_code == 1);
  CHECK(result->stderr_text == "Error: Flow has already been consumed\n");
}

TEST_CASE("Flow: parse_int yields some/none and keep_some drops the none items") {
  CHECK(stdout_of("stdin |> lines |> map(parse_int) |> collect",
                  "1\nx\n 7 \n-3\n+4\n1_0\n_1\n1_\n") ==
        "[some(1), none(\"parse-error\", {source: \"parse_int\", expected: \"integer_string\", "
        "received: \"x\", base: 10}), some(7), some(-3), some(4), some(10), "
        "none(\"parse-error\", {source: \"parse_int\", expected: \"integer_string\", "
        "received: \"_1\", base: 10}), none(\"parse-error\", {source: \"parse_int\", expected: "
        "\"integer_string\", received: \"1_\", base: 10})]\n");
  CHECK(stdout_of("stdin |> lines |> map(parse_int) |> keep_some |> collect", "1\nx\n 7 \n") ==
        "[1, 7]\n");
}

TEST_CASE("Flow: reduce and scan fold an unbounded source only as far as it is bounded") {
  CHECK(stdout_of("evolve(0, (n) -> n + 1) |> map((n) -> n * n) |> take(4) |> "
                  "reduce((a, b) -> a + b, 0)") == "14\n");
  CHECK(stdout_of("stdin |> lines |> scan((s, i) -> [s + 1, s], 0) |> take(2) |> collect",
                  "a\nb\nc\n") == "[0, 1]\n");
  CHECK(stdout_of("stdin |> lines |> reduce((acc, l) -> acc + l, \"\")", "a\nb\n") == "\"ab\"\n");
}

TEST_CASE("Flow: run displays nothing and each preserves items") {
  CHECK(stdout_of("stdin |> lines |> run", "a\n").empty());
  CHECK(stdout_of("stdin |> lines |> each(print) |> run", "a\nb\n") == "a\nb\n");
  CHECK(stdout_of("stdin |> lines |> each(print) |> collect", "a\nb\n") ==
        "a\nb\n[\"a\", \"b\"]\n");
}

TEST_CASE("Flow: list sources adapt through lines") {
  CHECK(stdout_of("[\"a\", \"b\"] |> lines |> map(upper) |> collect") == "[\"A\", \"B\"]\n");
  CHECK_FALSE(run_flow("[1, 2] |> lines |> collect").has_value());
}

TEST_CASE("Flow: arrow-form function definitions match the equals form") {
  CHECK(stdout_of("f(n) -> n + 1\nf(2)") == "3\n");
  CHECK(stdout_of("f(n) -> n + 1\nevolve(0, f) |> take(3) |> collect") == "[0, 1, 2]\n");
}

TEST_CASE("Flow: string natives decide only ASCII and leave the rest unsupported") {
  CHECK(stdout_of("trim(\"  a b  \")") == "\"a b\"\n");
  CHECK(stdout_of("upper(\"abc\")") == "\"ABC\"\n");
  CHECK(stdout_of("contains(\"abc\", \"\")") == "true\n");
  CHECK_FALSE(run_flow("upper(\"\xC3\xA9\")").has_value());
  CHECK_FALSE(run_flow("trim(\"\xC2\xA0\x61\xC2\xA0\")").has_value());
}

TEST_CASE("Flow: unevidenced shapes stay unsupported") {
  // Displaying a Flow value, a bare stdin source, and non-callable stages.
  CHECK_FALSE(run_flow("evolve(0, (n) -> n + 1) |> take(2)").has_value());
  // A bare stdin source reaching collect is a real diagnostic (see the E27-5
  // hardening tests below), no longer unsupported.
  CHECK(run_flow("stdin |> collect", "a\n")->exit_code == 1);
  CHECK_FALSE(run_flow("evolve(0, 5) |> take(2) |> collect").has_value());
  CHECK_FALSE(run_flow("stdin |> lines |> filter((l) -> 1) |> collect", "a\n").has_value());
}

TEST_CASE("Flow: eval passes stdin through the adapter and names the span <command>") {
  const nlohmann::json request = {
      {"protocol_version", 1},
      {"case_id", "flow-1"},
      {"operation", "eval"},
      {"input", {{"source", "stdin |> lines |> collect"}, {"stdin", "a\nb\n"}, {"argv", nullptr}}}};
  auto response = genia::adapter::handle_request(request.dump());
  REQUIRE(response.has_value());
  CHECK((*response)["status"] == "ok");
  CHECK((*response)["result"]["stdout"] == "[\"a\", \"b\"]\n");
}

TEST_CASE("Flow: file-mode CLI does not guess the pipeline span file name") {
  // try_run without a source name has no evidenced span file, so a stage
  // error is unsupported rather than printed with a guessed name.
  auto result = genia::engine::try_run("x = stdin |> lines\nx |> collect\nx |> collect", false,
                                       std::string("a\n"));
  CHECK_FALSE(result.has_value());
}

// ---- E27-5 hardening ------------------------------------------------------

TEST_CASE("Flow hardening: non-Seq values get the Seq-compatible diagnostics") {
  auto direct = run_flow("collect(5)");
  REQUIRE(direct.has_value());
  CHECK(direct->exit_code == 1);
  CHECK(direct->stderr_text ==
        "Error: collect expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("run(5)")->stderr_text ==
        "Error: run expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("each(print, 5)")->stderr_text ==
        "Error: each expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("map(upper, 5)")->stderr_text ==
        "Error: map expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("scan((s, i) -> [s, i], 0, 5)")->stderr_text ==
        "Error: scan expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("collect(stdin)", "a\n")->stderr_text ==
        "Error: collect expected a Seq-compatible value (list or Flow); received stdin. Use "
        "stdin |> lines to adapt stdin into a Flow.\n");
  CHECK(run_flow("keep_some(5)")->stderr_text ==
        "Error: keep_some expected a flow, received int\n");
}

TEST_CASE("Flow hardening: the Seq-compatible error is wrapped by its pipeline stage") {
  CHECK(run_flow("5 |> collect")->stderr_text ==
        "Error: pipeline stage 1 failed in Value mode at collect [<command>:1]: stage received "
        "int; collect expected a Seq-compatible value (list or Flow); received int.\n");
  CHECK(run_flow("stdin |> lines |> count |> collect", "1\n2\n3\n")->stderr_text ==
        "Error: pipeline stage 3 failed in Value mode at collect [<command>:1]: stage received "
        "int; collect expected a Seq-compatible value (list or Flow); received int.\n");
}

TEST_CASE("Flow hardening: count folds lists and Flows, concat joins strings only") {
  CHECK(stdout_of("count([\"a\", \"b\"])") == "2\n");
  CHECK(stdout_of("stdin |> lines |> count", "a\nb\n") == "2\n");
  CHECK(stdout_of("concat(\"a\", \"b\")") == "\"ab\"\n");
  CHECK_FALSE(run_flow("concat(\"a\", 1)").has_value());
  CHECK(
      stdout_of("[\"a\", \"b\"] |> lines |> each((x) -> print(concat(\"seen:\", x))) |> collect") ==
      "seen:a\nseen:b\n[\"a\", \"b\"]\n");
}

TEST_CASE("Flow hardening: pipelines inside call arguments stay unsupported") {
  // A parser gap, not a Flow one: the C++ host does not parse `f(a |> b)`.
  CHECK_FALSE(run_flow("print([\"a\"] |> lines |> collect)").has_value());
}
