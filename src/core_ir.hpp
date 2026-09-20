// Minimal portable Core IR nodes for the E24-2 vertical slice:
// IrLiteral, IrVar, and IrBinary only (genia-2026's
// docs/architecture/core-ir-portability.md minimal_portable_node_families
// list has 29 entries; this slice implements exactly the three its
// grammar can produce -- see AGENTS.md's "no shortcuts" rule: this is a
// real lowering step, not the parser AST reused as if it were IR).
//
// Wire shapes (only exercised internally by this slice; no `lower`
// bootstrap case is pinned yet, but these match
// hosts/python/ir_normalize.py's `_normalize_ir_node` exactly so future
// slices' `lower` operation evidence is not built on a guess):
//   IrLiteral:  {"node": "IrLiteral", "value": {"kind": "integer", "digits": "<text>"}}
//   IrVar:      {"node": "IrVar", "name": "<name>"}
//   IrBinary:   {"node": "IrBinary", "left": <ir>, "op": "PLUS"|"MINUS"|"STAR"|"SLASH",
//               "right": <ir>}
//   IrExprStmt: {"node": "IrExprStmt", "expr": <ir>}
//
// Every top-level program statement in this slice's grammar is an
// expression statement, so `lowering::lower_program` wraps each one in
// IrExprStmt (matching src/genia/lowering.py's `ExprStmt -> IrExprStmt`
// rule) -- confirmed directly against genia-2026's spec/ir/*.yaml
// evidence, e.g. r21-integer-literal-tagged-payload.yaml expects
// `[{"node": "IrExprStmt", "expr": {"node": "IrLiteral", ...}}]`, never
// a bare top-level IrLiteral. Nested operands (IrBinary's left/right)
// are never wrapped -- only whole top-level statements are.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace genia::core_ir {

enum class Kind : std::uint8_t { Literal, Var, Binary, ExprStmt };

// Binary operator token names, matching genia-2026's parser token names
// (see src/genia/lowering.py: `IrBinary(lower(left), node.op, lower(right))`
// where `node.op` is the raw lexer token name, not the symbol).
enum class Op : std::uint8_t { Plus, Minus, Star, Slash };

inline const char* op_token_name(Op op) {
  switch (op) {
    case Op::Plus:
      return "PLUS";
    case Op::Minus:
      return "MINUS";
    case Op::Star:
      return "STAR";
    case Op::Slash:
      return "SLASH";
  }
  return "";
}

struct Node {
  Kind kind = Kind::Literal;

  // Literal: canonical unsigned decimal digit text (R21 tagged payload
  // is {"kind": "integer", "digits": digits}).
  std::string integer_digits;

  // Var
  std::string name;

  // Binary
  Op op = Op::Plus;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

  // ExprStmt: the wrapped expression (reuses `left`).

  static Node literal(std::string digits) {
    Node n;
    n.kind = Kind::Literal;
    n.integer_digits = std::move(digits);
    return n;
  }

  static Node var(std::string identifier) {
    Node n;
    n.kind = Kind::Var;
    n.name = std::move(identifier);
    return n;
  }

  static Node binary(Op op, Node lhs, Node rhs) {
    Node n;
    n.kind = Kind::Binary;
    n.op = op;
    n.left = std::make_shared<Node>(std::move(lhs));
    n.right = std::make_shared<Node>(std::move(rhs));
    return n;
  }

  static Node expr_stmt(Node inner) {
    Node n;
    n.kind = Kind::ExprStmt;
    n.left = std::make_shared<Node>(std::move(inner));
    return n;
  }
};

}  // namespace genia::core_ir
