// R27 E27-2 CLI pipe mode (`genia -p '<stage expression>'`).
//
// Mirrors the reference host's pipe mode (src/genia/interpreter.py
// `_wrap_pipe_mode_expr` / `_validate_pipe_mode_expr`, src/genia/errors.py
// `_format_pipe_mode_error`): the stage expression is validated, wrapped as
// `stdin |> lines |> <expr> |> _pipe_run` and run with the pipeline-stage
// span file name `<pipe>`, and any error is rewritten into the mode's
// guidance text. Pipe mode never dispatches `main` and never displays a
// result: the implicit final `_pipe_run` consumes the last Flow.
//
// The rewriting rules implemented here are exactly those the pinned
// `requires: [cli_pipe_mode]` shared cases exercise plus the reference
// host's own adjacent branches, each cross-checked against `genia -p`. Any
// error message that would need a branch not implemented here (an Option
// receiver, `collect_validated`) makes the run unsupported instead.
#pragma once

#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ast.hpp"
#include "engine.hpp"
#include "parser.hpp"

namespace genia::pipe_mode {

inline void bind_pattern_names(const pattern::Pattern& p, std::set<std::string>& names) {
  if ((p.kind == pattern::Kind::Bind || p.kind == pattern::Kind::Rest) && !p.name.empty()) {
    names.insert(p.name);
  }
  for (const auto& item : p.items) bind_pattern_names(item, names);
  for (const auto& entry : p.map_items) bind_pattern_names(entry.value, names);
}

struct ReservedUsage {
  bool stdin_name = false;
  bool run_name = false;
};

// Mirrors `_scan_pipe_mode_reserved_usage`: an unbound `stdin` or `run`
// reference (including as a call's callee name). Lambda parameters and names
// assigned earlier in a block shadow them.
inline void scan_reserved(const ast::Node& node, std::set<std::string> bound,
                          ReservedUsage& usage) {
  auto note = [&](const std::string& name) {
    if (bound.count(name) > 0) return;
    if (name == "stdin") usage.stdin_name = true;
    if (name == "run") usage.run_name = true;
  };
  switch (node.kind) {
    case ast::Kind::Var:
      note(node.name);
      return;
    case ast::Kind::Call:
      note(node.name);
      break;
    case ast::Kind::Lambda:
      for (const auto& param : node.params) bind_pattern_names(param, bound);
      break;
    case ast::Kind::Block:
      for (const auto& expression : node.items) {
        scan_reserved(expression, bound, usage);
        if (expression.kind == ast::Kind::Assign) bound.insert(expression.name);
      }
      return;
    default:
      break;
  }
  if (node.left) scan_reserved(*node.left, bound, usage);
  if (node.right) scan_reserved(*node.right, bound, usage);
  for (const auto& item : node.items) scan_reserved(item, bound, usage);
  for (const auto& entry : node.map_entries) scan_reserved(entry.value, bound, usage);
  for (const auto& result : node.case_results) scan_reserved(result, bound, usage);
}

inline bool ends_word(const std::string& text, size_t end) {
  return end >= text.size() ||
         !(std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_');
}

inline bool has_expected_scalar_flow(const std::string& message) {
  for (const char* type : {"string", "number", "integer", "bool"}) {
    const std::string needle = std::string("expected a ") + type + ", received flow";
    const size_t at = message.find(needle);
    if (at != std::string::npos && ends_word(message, at + needle.size())) return true;
  }
  return false;
}

// `_extract_pipe_stage_name`: the stage text between " at " and " [".
inline std::string extract_stage_name(const std::string& message) {
  const size_t at = message.find(" at ");
  if (at == std::string::npos) return "";
  const size_t end = message.find(" [", at + 4);
  if (end == std::string::npos || end == at + 4) return "";
  return message.substr(at + 4, end - (at + 4));
}

// `_format_pipe_mode_error` for runtime errors. std::nullopt = a branch this
// host does not implement (unsupported).
inline std::optional<std::string> format_runtime_error(const std::string& message,
                                                       const std::string& stage_expr) {
  if (message.find("Flow has already been consumed") != std::string::npos) {
    return "Flow values are single-use and cannot be reused after consumption";
  }
  const std::string run_marker = "run expected a flow, received ";
  if (message.find(run_marker) != std::string::npos) {
    if (stage_expr.find("collect_validated") != std::string::npos) return std::nullopt;
    const std::string received = message.substr(message.rfind("received ") + 9);
    const std::string detail = "Pipe mode stage must produce a flow; received " + received;
    for (const char* known : {"int", "float", "bool", "string", "list", "map"}) {
      if (received == known) {
        return detail +
               ". Use -c/--command when you want a final value such as `collect |> sum` or "
               "`collect |> count`.";
      }
    }
    if (received == "some" || received == "none") return std::nullopt;
    return detail;
  }
  if (message.find("stage received flow;") != std::string::npos) {
    const std::string stage_name = extract_stage_name(message);
    if (has_expected_scalar_flow(message)) {
      std::string out =
          message + "\nPipe mode passes a Flow through each stage, not one item at a time.\n";
      out += stage_name.empty()
                 ? "Wrap per-item functions with map(...) or keep_some(...)."
                 : "Did you mean: map(" + stage_name + ") or keep_some(" + stage_name + ")";
      return out;
    }
    if (message.find("expected a list, received flow") != std::string::npos) {
      std::string out = message + "\nPipe mode passes a Flow, not a materialized list.";
      if (!stage_name.empty()) out += "\nDid you mean: collect |> " + stage_name;
      return out + "\nOr use -c/--command for the full pipeline.";
    }
    return message +
           ". Pipe mode stages receive a Flow, not one row at a time. Use Flow stages such as "
           "map(...), filter(...), head(...), each(...), or keep_some(...); use -c/--command for "
           "reducers such as sum or count.";
  }
  if (message.find("received some") != std::string::npos) return std::nullopt;
  return message;
}

inline engine::RunResult usage_error(const std::string& message) {
  engine::RunResult result;
  result.stderr_text = "Error: " + message + "\n";
  result.exit_code = 1;
  return result;
}

// Runs `genia -p <stage_expr>` over `stdin_text`. std::nullopt = unsupported.
inline std::optional<engine::RunResult> try_run_pipe(const std::string& stage_expr,
                                                     const std::string& stdin_text) {
  auto program = parser::parse_program(stage_expr);
  if (!program.has_value()) return std::nullopt;
  if (program->size() != 1 || (*program)[0].kind == ast::Kind::FuncDef ||
      (*program)[0].kind == ast::Kind::OpenFuncDef || (*program)[0].kind == ast::Kind::Assign) {
    return usage_error(
        "Pipe mode expression must be a single stage expression, not a full program");
  }
  ReservedUsage usage;
  scan_reserved((*program)[0], {}, usage);
  if (usage.stdin_name) {
    return usage_error("Do not use stdin in pipe mode; stdin is provided automatically");
  }
  if (usage.run_name) {
    return usage_error("Do not use run in pipe mode; run is implicit in pipe mode");
  }
  const std::string wrapped = "stdin |> lines |> " + stage_expr + " |> _pipe_run";
  return engine::try_run(
      wrapped, false, stdin_text, "<pipe>",
      [&stage_expr](const std::string& message) {
        return format_runtime_error(message, stage_expr);
      },
      std::vector<std::string>{});
}

}  // namespace genia::pipe_mode
