// Ties parser -> Core IR -> evaluator -> normalized result together for
// the E24-2 vertical slice's `parse`, `lower`, `eval`, and `cli`
// operations. This is the one place genia-cpp's mandatory layering
// (docs/design/r24-cpp-host-preflight.md section 2) is enforced: every
// operation here goes through the real parser and real Core IR, never a
// shortcut that skips straight from source text to a printed value.
#pragma once

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "ast_projection.hpp"
#include "evaluator.hpp"
#include "format.hpp"
#include "ir_projection.hpp"
#include "lowering.hpp"
#include "parser.hpp"
#include "render.hpp"
#include "runtime_io.hpp"

namespace genia::engine {

using json = nlohmann::json;

struct RunResult {
  std::string stdout_text;
  std::string stderr_text;
  int exit_code = 0;
};

// `parse` operation: returns the parse-category AST projection, or
// std::nullopt if `source` is outside this slice's grammar (caller must
// report the case as unsupported).
inline std::optional<json> try_parse(const std::string& source) {
  auto program = parser::parse_program(source);
  if (!program.has_value()) {
    return std::nullopt;
  }
  return ast_projection::project_program(*program);
}

// `lower` operation: returns the normalized Core IR array, or
// std::nullopt if `source` is outside this slice's grammar.
inline std::optional<json> try_lower(const std::string& source) {
  auto program = parser::parse_program(source);
  if (!program.has_value()) {
    return std::nullopt;
  }
  auto ir_program = lowering::lower_program(*program);
  if (!ir_program.has_value()) {
    return std::nullopt;
  }
  return ir_projection::project_program(*ir_program);
}

// Runs `source` end to end (parse -> lower -> eval) and produces the
// command-mode auto-display output: the final statement's rendered
// value plus a trailing newline, matching genia-2026's `_emit_result`
// convention. Returns std::nullopt if any stage cannot honestly handle
// `source` (unsupported grammar, undefined name, non-integer operand,
// or a division this slice cannot represent exactly) -- the caller must
// report the whole case as unsupported, never emit a guessed result.
inline std::optional<RunResult> try_run(const std::string& source, bool enable_r25_fixture = false,
                                        const std::optional<std::string>& stdin_text = std::nullopt,
                                        const std::string& source_name = "") {
  struct FixtureGuard {
    bool previous;
    explicit FixtureGuard(bool enabled) : previous(value::g_r25_fixture_enabled) {
      value::g_r25_fixture_enabled = enabled;
    }
    ~FixtureGuard() { value::g_r25_fixture_enabled = previous; }
  } fixture_guard(enable_r25_fixture);
  // R27 E27-1: `print` output is captured here and `stdin |> lines` reads
  // from `stdin_text`. Carriage returns in stdin are not evidenced, so such
  // input is unsupported rather than guessed.
  std::shared_ptr<runtime_io::StdinState> stdin_state;
  if (stdin_text.has_value()) {
    auto parsed_stdin = runtime_io::make_stdin(*stdin_text);
    if (!parsed_stdin.has_value()) {
      return std::nullopt;
    }
    stdin_state = *parsed_stdin;
  }
  runtime_io::RunGuard run_guard(std::move(stdin_state), source_name);
  auto program = parser::parse_program(source);
  if (!program.has_value()) {
    return std::nullopt;
  }
  auto ir_program = lowering::lower_program(*program);
  if (!ir_program.has_value()) {
    return std::nullopt;
  }
  // Any error after some `print` output still carries that output on stdout.
  auto error_result = [](const std::string& message) {
    RunResult run_result;
    run_result.stdout_text = runtime_io::g_stdout_text;
    run_result.stderr_text = "Error: " + message + "\n";
    run_result.exit_code = 1;
    return run_result;
  };
  std::optional<value::Value> result;
  try {
    result = evaluator::eval_program(*ir_program);
  } catch (const evaluator::UndefinedNameError& error) {
    // E24-4's `deterministic_runtime_error_behavior` evidence
    // (error-undefined-name.yaml): genia-2026's real reference host
    // normalizes any undefined-name reference, at any point during
    // command-mode execution, to exactly this stderr text and exit
    // code -- see evaluator.hpp's header comment for the verified
    // source (src/genia/environment.py + src/genia/interpreter.py).
    // This is a genuine "ok" adapter result (the program ran and
    // produced a real, deterministic error), never "unsupported".
    return error_result("Undefined name: " + error.name);
  } catch (const evaluator::StatefulRuntimeError& error) {
    return error_result(error.message);
  } catch (const value::FlowError& error) {
    return error_result(error.message);
  } catch (const value::UnsupportedError&) {
    return std::nullopt;
  } catch (const float64::MagnitudeOverflowError&) {
    return error_result("float64: exact magnitude exceeds the largest finite binary64 value");
  } catch (const float64::DivisionByZeroError&) {
    return error_result("float64 division by zero");
  } catch (const float64::RemainderByZeroError&) {
    return error_result("float64 remainder by zero");
  } catch (const format::FormatError& error) {
    return error_result(error.message);
  }
  if (!result.has_value()) {
    return std::nullopt;
  }
  RunResult run_result;
  run_result.stdout_text = runtime_io::g_stdout_text;
  run_result.exit_code = 0;
  if (result->kind == value::Kind::Nil) {
    // The reference host's `None` (for example from `run`) displays nothing.
    return run_result;
  }
  auto rendered = render::display(*result);
  if (!rendered.has_value()) {
    return std::nullopt;
  }
  run_result.stdout_text += *rendered + "\n";
  return run_result;
}

// Scripted REPL sessions share one environment across complete submissions.
// Parsing the accumulated source determines when a submission is complete.
inline std::optional<RunResult> try_repl(const std::string& input) {
  auto env = evaluator::new_session_environment();
  RunResult output;
  std::istringstream lines(input);
  std::string line;
  std::string pending;
  while (std::getline(lines, line)) {
    pending += line + "\n";
    auto parsed = parser::parse_program(pending);
    if (!parsed.has_value()) continue;
    auto lowered = lowering::lower_program(*parsed);
    if (!lowered.has_value()) return std::nullopt;
    pending.clear();
    if (lowered->empty()) continue;
    try {
      auto result = evaluator::eval_in_environment(*lowered, env);
      if (!result.has_value()) return std::nullopt;
      auto rendered = render::display(*result);
      if (!rendered.has_value()) return std::nullopt;
      output.stdout_text += *rendered + "\n";
    } catch (const evaluator::UndefinedNameError& error) {
      output.stderr_text += "Error: Undefined name: " + error.name + "\n";
    } catch (const evaluator::StatefulRuntimeError& error) {
      output.stderr_text += "Error: " + error.message + "\n";
    } catch (const value::FlowError&) {
      return std::nullopt;
    } catch (const value::UnsupportedError&) {
      return std::nullopt;
    } catch (const float64::MagnitudeOverflowError&) {
      output.stderr_text +=
          "Error: float64: exact magnitude exceeds the largest finite binary64 value\n";
    } catch (const float64::DivisionByZeroError&) {
      output.stderr_text += "Error: float64 division by zero\n";
    } catch (const float64::RemainderByZeroError&) {
      output.stderr_text += "Error: float64 remainder by zero\n";
    } catch (const format::FormatError& error) {
      output.stderr_text += "Error: " + error.message + "\n";
    }
  }
  return output;
}

}  // namespace genia::engine
