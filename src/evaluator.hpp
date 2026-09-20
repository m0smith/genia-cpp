// Core IR evaluator for the E24-2 vertical slice: exact Integer
// arithmetic plus bare global-name lookup only.
//
// Returns std::nullopt whenever the specific IR node cannot be honestly
// evaluated by this slice (an undefined name, a non-integer operand, or
// a division that would require a Rational this slice does not
// implement -- see bignum.hpp's exact_divide). Callers must treat
// std::nullopt as "this case is unsupported", never attempt a fallback
// value or a best-effort diagnostic: this project's ambiguity-stop rule
// applies as much to runtime behavior as to parsing.
#pragma once

#include <optional>
#include <vector>

#include "bignum.hpp"
#include "core_ir.hpp"
#include "global_env.hpp"
#include "value.hpp"

namespace genia::evaluator {

inline std::optional<value::Value> eval_node(const core_ir::Node& node) {
  switch (node.kind) {
    case core_ir::Kind::ExprStmt:
      return eval_node(*node.left);
    case core_ir::Kind::Literal: {
      auto integer = bignum::Integer::from_unsigned_decimal(node.integer_digits);
      if (!integer.has_value()) {
        return std::nullopt;
      }
      return value::Value::make_integer(*integer);
    }
    case core_ir::Kind::Var: {
      return global_env::lookup(node.name);
    }
    case core_ir::Kind::Binary: {
      auto lhs = eval_node(*node.left);
      auto rhs = eval_node(*node.right);
      if (!lhs.has_value() || !rhs.has_value()) {
        return std::nullopt;
      }
      if (lhs->kind != value::Kind::Integer || rhs->kind != value::Kind::Integer) {
        return std::nullopt;
      }
      switch (node.op) {
        case core_ir::Op::Plus:
          return value::Value::make_integer(lhs->integer.add(rhs->integer));
        case core_ir::Op::Minus:
          return value::Value::make_integer(lhs->integer.sub(rhs->integer));
        case core_ir::Op::Star:
          return value::Value::make_integer(lhs->integer.mul(rhs->integer));
        case core_ir::Op::Slash: {
          auto quotient = lhs->integer.exact_divide(rhs->integer);
          if (!quotient.has_value()) {
            // Division by zero, or a non-evenly-dividing quotient that
            // R22 defines as producing a Rational: this slice
            // implements neither, so the case is unsupported rather
            // than wrong.
            return std::nullopt;
          }
          return value::Value::make_integer(*quotient);
        }
      }
      return std::nullopt;
    }
  }
  return std::nullopt;
}

// Evaluates a full program (sequence of independent top-level
// statements), returning the last statement's value -- matching
// genia-2026's block-evaluation model (the value of a program/block is
// the value of its last expression). Returns std::nullopt if the
// program is empty or if any statement cannot be evaluated.
inline std::optional<value::Value> eval_program(const std::vector<core_ir::Node>& program) {
  if (program.empty()) {
    return std::nullopt;
  }
  std::optional<value::Value> result;
  for (const auto& node : program) {
    result = eval_node(node);
    if (!result.has_value()) {
      return std::nullopt;
    }
  }
  return result;
}

}  // namespace genia::evaluator
