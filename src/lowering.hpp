// AST -> Core IR lowering for the E24-2..E24-4 vertical slice.
//
// A real lowering step, matching genia-2026's src/genia/lowering.py
// approach of one dedicated function translating parser-AST node kinds
// into Core IR node kinds -- never the parser AST reused directly as
// evaluator input (AGENTS.md's "no parser-to-runtime shortcuts" rule).
#pragma once

#include <optional>
#include <vector>

#include "ast.hpp"
#include "core_ir.hpp"

namespace genia::lowering {

inline std::optional<core_ir::Op> lower_op(const std::string& symbol) {
  if (symbol == "+") return core_ir::Op::Plus;
  if (symbol == "-") return core_ir::Op::Minus;
  if (symbol == "*") return core_ir::Op::Star;
  if (symbol == "/") return core_ir::Op::Slash;
  if (symbol == "%") return core_ir::Op::Percent;
  if (symbol == "==") return core_ir::Op::EqEq;
  if (symbol == "!=") return core_ir::Op::NotEq;
  if (symbol == "<") return core_ir::Op::Lt;
  if (symbol == "<=") return core_ir::Op::Le;
  if (symbol == ">") return core_ir::Op::Gt;
  if (symbol == ">=") return core_ir::Op::Ge;
  return std::nullopt;
}

// A pipeline chain parses as nested left-associative Binary("|>") nodes
// (`a |> b |> c` == `Binary(Binary(a, "|>", b), "|>", c)`); this
// flattens it into one source expression plus an ordered stage list,
// matching genia-2026's src/genia/lowering.py `_flatten_pipeline_ast`.
inline std::optional<std::pair<ast::Node, std::vector<ast::Node>>> flatten_pipeline(
    const ast::Node& node) {
  if (node.kind == ast::Kind::Binary && node.op == "|>") {
    auto flattened = flatten_pipeline(*node.left);
    if (!flattened.has_value()) {
      return std::nullopt;
    }
    flattened->second.push_back(*node.right);
    return flattened;
  }
  return std::make_pair(node, std::vector<ast::Node>{});
}

inline std::optional<core_ir::Node> lower_node(const ast::Node& node) {
  switch (node.kind) {
    case ast::Kind::Literal:
      return core_ir::Node::integer_literal(node.integer_digits);
    case ast::Kind::DecimalLiteral:
      return core_ir::Node::decimal_literal(node.decimal_coefficient_digits, node.decimal_exponent);
    case ast::Kind::StringLiteral:
      return core_ir::Node::string_literal(node.string_value);
    case ast::Kind::BoolLiteral:
      return core_ir::Node::bool_literal(node.bool_value);
    case ast::Kind::Var:
      return core_ir::Node::var(node.name);
    case ast::Kind::Unary: {
      auto op = lower_op(node.op);
      if (!op.has_value()) {
        return std::nullopt;
      }
      auto operand = lower_node(*node.left);
      if (!operand.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::unary(*op, std::move(*operand));
    }
    case ast::Kind::Quote:
    case ast::Kind::QuasiQuote: {
      auto inner = lower_node(*node.left);
      if (!inner.has_value()) return std::nullopt;
      return core_ir::Node::quote(std::move(*inner), node.kind == ast::Kind::QuasiQuote);
    }
    case ast::Kind::Binary: {
      if (node.op == "|>") {
        auto flattened = flatten_pipeline(node);
        if (!flattened.has_value()) {
          return std::nullopt;
        }
        auto source = lower_node(flattened->first);
        if (!source.has_value()) {
          return std::nullopt;
        }
        std::vector<core_ir::Node> stages;
        stages.reserve(flattened->second.size());
        for (const auto& stage : flattened->second) {
          auto lowered_stage = lower_node(stage);
          if (!lowered_stage.has_value()) {
            return std::nullopt;
          }
          stages.push_back(std::move(*lowered_stage));
        }
        return core_ir::Node::pipeline(std::move(*source), std::move(stages));
      }
      auto op = lower_op(node.op);
      if (!op.has_value()) {
        return std::nullopt;
      }
      auto left = lower_node(*node.left);
      auto right = lower_node(*node.right);
      if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::binary(*op, std::move(*left), std::move(*right));
    }
    case ast::Kind::List: {
      std::vector<core_ir::Node> items;
      items.reserve(node.items.size());
      for (const auto& item : node.items) {
        auto lowered = lower_node(item);
        if (!lowered.has_value()) {
          return std::nullopt;
        }
        items.push_back(std::move(*lowered));
      }
      return core_ir::Node::list(std::move(items));
    }
    case ast::Kind::Assign: {
      auto value = lower_node(*node.left);
      if (!value.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::assign(node.name, std::move(*value));
    }
    case ast::Kind::Call: {
      std::vector<core_ir::Node> args;
      args.reserve(node.items.size());
      for (const auto& arg : node.items) {
        auto lowered = lower_node(arg);
        if (!lowered.has_value()) {
          return std::nullopt;
        }
        args.push_back(std::move(*lowered));
      }
      return core_ir::Node::call(node.name, std::move(args));
    }
    case ast::Kind::Lambda: {
      if (node.is_case_body) {
        // No pinned E24-4 evidence needs a case-bodied bare lambda
        // (only FuncDef does -- see map_acc/get_name); the parser never
        // produces one, so this path is unreachable, not a guess.
        return std::nullopt;
      }
      auto body = lower_node(*node.left);
      if (!body.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::lambda(node.params, std::move(*body));
    }
    case ast::Kind::FuncDef: {
      if (node.is_case_body) {
        std::vector<core_ir::Node> results;
        results.reserve(node.case_results.size());
        for (const auto& result : node.case_results) {
          auto lowered_result = lower_node(result);
          if (!lowered_result.has_value()) {
            return std::nullopt;
          }
          results.push_back(std::move(*lowered_result));
        }
        return core_ir::Node::func_def_case(node.name, node.header_param_names, node.case_patterns,
                                            std::move(results));
      }
      auto body = lower_node(*node.left);
      if (!body.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::func_def(node.name, node.header_param_names, node.params,
                                     std::move(*body));
    }
    case ast::Kind::OpenFuncDef: {
      std::vector<core_ir::Node> results;
      results.reserve(node.case_results.size());
      for (const auto& result : node.case_results) {
        auto lowered_result = lower_node(result);
        if (!lowered_result.has_value()) {
          return std::nullopt;
        }
        results.push_back(std::move(*lowered_result));
      }
      return core_ir::Node::open_func_def(node.name, node.case_patterns, std::move(results));
    }
    case ast::Kind::Map: {
      std::vector<std::pair<std::string, core_ir::Node>> entries;
      entries.reserve(node.map_entries.size());
      for (const auto& [key, value_node] : node.map_entries) {
        auto lowered_value = lower_node(value_node);
        if (!lowered_value.has_value()) {
          return std::nullopt;
        }
        entries.emplace_back(key, std::move(*lowered_value));
      }
      return core_ir::Node::map_literal(std::move(entries));
    }
    case ast::Kind::Spread: {
      auto inner = lower_node(*node.left);
      if (!inner.has_value()) {
        return std::nullopt;
      }
      return core_ir::Node::spread(std::move(*inner));
    }
  }
  return std::nullopt;
}

// Every top-level statement in this slice's grammar is an expression
// statement, an assignment, or (E24-4) a function definition.
// Expression statements lower wrapped in IrExprStmt (matching
// src/genia/lowering.py's `ExprStmt -> IrExprStmt` rule); IrAssign and
// IrFuncDef are never wrapped -- genia-2026's parser never produces an
// ExprStmt wrapping either (both are their own distinct top-level
// grammar productions, not expressions), so lowering never wraps them
// either (see src/genia/lowering.py: `lower_node` maps `FuncDef` and
// `Assign` straight to `IrFuncDef`/`IrAssign`, only `ExprStmt` produces
// `IrExprStmt`). Verified directly against genia-2026's spec/ir/*.yaml
// evidence for IrAssign; no pinned `ir`-category evidence yet exercises
// a top-level IrFuncDef, but the same non-wrapping rule follows directly
// from the same parser/lowering source.
inline std::optional<std::vector<core_ir::Node>> lower_program(const ast::Program& program) {
  std::vector<core_ir::Node> result;
  result.reserve(program.size());
  for (const auto& node : program) {
    auto lowered = lower_node(node);
    if (!lowered.has_value()) {
      return std::nullopt;
    }
    if (lowered->kind == core_ir::Kind::Assign || lowered->kind == core_ir::Kind::FuncDef ||
        lowered->kind == core_ir::Kind::OpenFuncDef) {
      result.push_back(std::move(*lowered));
    } else {
      result.push_back(core_ir::Node::expr_stmt(std::move(*lowered)));
    }
  }
  return result;
}

}  // namespace genia::lowering
