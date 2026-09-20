// AST -> Core IR lowering for the E24-2/E24-3 vertical slice.
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
  if (symbol == "==") return core_ir::Op::EqEq;
  return std::nullopt;
}

inline std::optional<core_ir::Node> lower_node(const ast::Node& node) {
  switch (node.kind) {
    case ast::Kind::Literal:
      return core_ir::Node::integer_literal(node.integer_digits);
    case ast::Kind::StringLiteral:
      return core_ir::Node::string_literal(node.string_value);
    case ast::Kind::BoolLiteral:
      return core_ir::Node::bool_literal(node.bool_value);
    case ast::Kind::Var:
      return core_ir::Node::var(node.name);
    case ast::Kind::Binary: {
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
  }
  return std::nullopt;
}

// Every top-level statement in this slice's grammar is an expression
// statement or an assignment. Expression statements lower wrapped in
// IrExprStmt (matching src/genia/lowering.py's `ExprStmt -> IrExprStmt`
// rule); IrAssign is never wrapped, per GENIA_RULES.md's "IrAssign
// placement" invariant (it appears directly in IrBlock.exprs). Both
// verified directly against genia-2026's spec/ir/*.yaml evidence.
inline std::optional<std::vector<core_ir::Node>> lower_program(const ast::Program& program) {
  std::vector<core_ir::Node> result;
  result.reserve(program.size());
  for (const auto& node : program) {
    auto lowered = lower_node(node);
    if (!lowered.has_value()) {
      return std::nullopt;
    }
    if (lowered->kind == core_ir::Kind::Assign) {
      result.push_back(std::move(*lowered));
    } else {
      result.push_back(core_ir::Node::expr_stmt(std::move(*lowered)));
    }
  }
  return result;
}

}  // namespace genia::lowering
