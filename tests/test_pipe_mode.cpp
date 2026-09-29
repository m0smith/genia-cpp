// R27 E27-2 pipe-mode tests. Expected values were checked against the
// Python reference host (`genia -p`); the shared `requires: [cli_pipe_mode]`
// specs in genia-2026 remain the conformance evidence.
#include <string>

#include "../src/adapter.hpp"
#include "../src/engine.hpp"
#include "../src/pipe_mode.hpp"
#include "../third_party/catch2/catch.hpp"

namespace {

using genia::engine::RunResult;

RunResult pipe(const std::string& expr, const std::string& stdin_text) {
  auto result = genia::pipe_mode::try_run_pipe(expr, stdin_text);
  REQUIRE(result.has_value());
  return *result;
}

}  // namespace

TEST_CASE("Pipe: stage expression runs over stdin lines and consumes the final Flow") {
  auto result = pipe("each(print)", "alpha\nbeta\n");
  CHECK(result.exit_code == 0);
  CHECK(result.stdout_text == "alpha\nbeta\n");
  CHECK(result.stderr_text.empty());
  CHECK(pipe("map(parse_int) |> keep_some |> take(2) |> each(print)", "1\nx\n3\n4\n").stdout_text ==
        "1\n3\n");
  CHECK(pipe("each(print)", "").stdout_text.empty());
}

TEST_CASE("Pipe: a bounded stage does not over-read stdin") {
  CHECK(pipe("take(1) |> each(print)", "a\nb\n").stdout_text == "a\n");
}

TEST_CASE("Pipe: argv() is the empty list and print displays it") {
  CHECK(pipe("each((line) -> print(argv()))", "x\ny\n").stdout_text == "[]\n[]\n");
}

TEST_CASE("Pipe: argv() stays unsupported outside pipe mode") {
  CHECK_FALSE(genia::engine::try_run("argv()").has_value());
}

TEST_CASE("Pipe: explicit unbound stdin and run are rejected before running") {
  auto stdin_use = pipe("stdin", "x\n");
  CHECK(stdin_use.exit_code == 1);
  CHECK(stdin_use.stdout_text.empty());
  CHECK(stdin_use.stderr_text ==
        "Error: Do not use stdin in pipe mode; stdin is provided automatically\n");
  CHECK(pipe("run", "x\n").stderr_text ==
        "Error: Do not use run in pipe mode; run is implicit in pipe mode\n");
  CHECK(pipe("each(run)", "x\n").stderr_text ==
        "Error: Do not use run in pipe mode; run is implicit in pipe mode\n");
}

TEST_CASE("Pipe: lambda parameters shadow the reserved names") {
  CHECK(pipe("map((run) -> run) |> each(print)", "x\n").stdout_text == "x\n");
  CHECK(pipe("each((stdin) -> print(stdin))", "y\n").stdout_text == "y\n");
}

TEST_CASE("Pipe: a full program or empty expression is not a stage expression") {
  const std::string expected =
      "Error: Pipe mode expression must be a single stage expression, not a full program\n";
  CHECK(pipe("x = 1", "a\n").stderr_text == expected);
  CHECK(pipe("", "a\n").stderr_text == expected);
  CHECK(pipe("f(x) -> x", "a\n").stderr_text == expected);
}

TEST_CASE("Pipe: a non-Flow final result reports what it received") {
  CHECK(pipe("collect", "10\n20\n").stderr_text ==
        "Error: Pipe mode stage must produce a flow; received list. Use -c/--command when you "
        "want a final value such as `collect |> sum` or `collect |> count`.\n");
}

TEST_CASE("Pipe: a bare per-item stage gets map/keep_some guidance") {
  auto result = pipe("parse_int", "10\n");
  CHECK(result.exit_code == 1);
  CHECK(result.stderr_text ==
        "Error: pipeline stage 2 failed in Flow mode at parse_int [<pipe>:1]: stage received "
        "flow; parse_int expected a string, received flow\n"
        "Pipe mode passes a Flow through each stage, not one item at a time.\n"
        "Did you mean: map(parse_int) or keep_some(parse_int)\n");
  CHECK(pipe("contains(\"a\")", "a\n").stderr_text ==
        "Error: pipeline stage 2 failed in Flow mode at contains(\"a\") [<pipe>:1]: stage "
        "received flow; contains expected a string, received flow\n"
        "Pipe mode passes a Flow through each stage, not one item at a time.\n"
        "Did you mean: map(contains(\"a\")) or keep_some(contains(\"a\"))\n");
}

TEST_CASE("Pipe: a bare reducer stage gets collect guidance") {
  CHECK(pipe("sum", "10\n").stderr_text ==
        "Error: pipeline stage 2 failed in Flow mode at sum [<pipe>:1]: stage received flow; "
        "sum expected a list, received flow\n"
        "Pipe mode passes a Flow, not a materialized list.\n"
        "Did you mean: collect |> sum\n"
        "Or use -c/--command for the full pipeline.\n");
}

TEST_CASE("Pipe: an undefined name in a stage keeps the generic Flow-stage guidance") {
  CHECK(pipe("undefined_fn", "a\n").stderr_text ==
        "Error: pipeline stage 2 failed in Flow mode at undefined_fn [<pipe>:1]: stage received "
        "flow; Undefined name: undefined_fn. Pipe mode stages receive a Flow, not one row at a "
        "time. Use Flow stages such as map(...), filter(...), head(...), each(...), or "
        "keep_some(...); use -c/--command for reducers such as sum or count.\n");
}

TEST_CASE("Pipe: output printed before an error stays on stdout") {
  auto result = pipe("each(print) |> collect", "a\n");
  CHECK(result.stdout_text == "a\n");
  CHECK(result.exit_code == 1);
}

TEST_CASE("Pipe: error rewriting follows the reference host's branch order") {
  using genia::pipe_mode::format_runtime_error;
  // Not reachable from one stage expression (a Flow cannot be named twice), so
  // the mapping is pinned directly from src/genia/errors.py.
  CHECK(*format_runtime_error("pipeline stage 3 failed in Explicit bridge mode at collect "
                              "[<pipe>:1]: stage received flow; Flow has already been consumed",
                              "x") ==
        "Flow values are single-use and cannot be reused after consumption");
  CHECK(*format_runtime_error("run expected a flow, received stdin", "x") ==
        "Pipe mode stage must produce a flow; received stdin");
  // Option receivers and collect_validated need branches that are not implemented.
  CHECK_FALSE(format_runtime_error("run expected a flow, received some", "x").has_value());
  CHECK_FALSE(format_runtime_error("run expected a flow, received map", "a |> collect_validated")
                  .has_value());
  CHECK_FALSE(format_runtime_error("x received some", "x").has_value());
  CHECK(*format_runtime_error("plain failure", "x") == "plain failure");
}

TEST_CASE("Pipe: shapes the reference host rewrites in ways not implemented are unsupported") {
  // collect_validated / json_compat are Python-host-only; Option receivers are
  // not rewritten here.
  CHECK_FALSE(
      genia::pipe_mode::try_run_pipe("map(parse_jsonl_record) |> collect_validated", "{\"id\":1}\n")
          .has_value());
}

TEST_CASE("Pipe: -p through the adapter needs exactly a stage expression and stdin") {
  auto request = [](const nlohmann::json& argv, const nlohmann::json& stdin_text) {
    return nlohmann::json{{"protocol_version", 1},
                          {"case_id", "p"},
                          {"operation", "cli"},
                          {"input", {{"argv", argv}, {"stdin", stdin_text}}}};
  };
  auto ok = genia::adapter::handle_request(request({"-p", "each(print)"}, "a\n").dump());
  REQUIRE(ok.has_value());
  CHECK((*ok)["status"] == "ok");
  CHECK((*ok)["result"]["stdout"] == "a\n");
  // Trailing script arguments and a missing stdin are not evidenced.
  auto trailing = genia::adapter::handle_request(request({"-p", "each(print)", "z"}, "a\n").dump());
  CHECK((*trailing)["status"] == "unsupported");
  auto no_stdin = genia::adapter::handle_request(request({"-p", "each(print)"}, nullptr).dump());
  CHECK((*no_stdin)["status"] == "unsupported");
}

TEST_CASE("Flow stage wrapper applies to undefined names under -c too") {
  auto result = genia::engine::try_run("1 |> nope", false, std::nullopt, "<command>");
  REQUIRE(result.has_value());
  CHECK(result->stderr_text ==
        "Error: pipeline stage 1 failed in Value mode at nope [<command>:1]: stage received int; "
        "Undefined name: nope\n");
  // File mode has no evidenced span file name, so the wrapped form is unsupported.
  CHECK_FALSE(genia::engine::try_run("1 |> nope").has_value());
}
