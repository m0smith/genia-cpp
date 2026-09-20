// Ties parser -> Core IR -> evaluator -> normalized result together for
// the E24-2 vertical slice's `parse`, `lower`, `eval`, and `cli`
// operations. This is the one place genia-cpp's mandatory layering
// (docs/design/r24-cpp-host-preflight.md section 2) is enforced: every
// operation here goes through the real parser and real Core IR, never a
// shortcut that skips straight from source text to a printed value.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../third_party/nlohmann_json/json.hpp"
#include "ast_projection.hpp"
#include "evaluator.hpp"
#include "ir_projection.hpp"
#include "lowering.hpp"
#include "parser.hpp"
#include "render.hpp"

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
inline std::optional<RunResult> try_run(const std::string& source) {
  auto program = parser::parse_program(source);
  if (!program.has_value()) {
    return std::nullopt;
  }
  auto ir_program = lowering::lower_program(*program);
  if (!ir_program.has_value()) {
    return std::nullopt;
  }
  auto result = evaluator::eval_program(*ir_program);
  if (!result.has_value()) {
    return std::nullopt;
  }
  auto rendered = render::display(*result);
  if (!rendered.has_value()) {
    return std::nullopt;
  }
  RunResult run_result;
  run_result.stdout_text = *rendered + "\n";
  run_result.exit_code = 0;
  return run_result;
}

}  // namespace genia::engine
