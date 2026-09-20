// Minimal portable Core IR nodes for the E24-2/E24-3 vertical slice:
// IrLiteral, IrVar, IrBinary, IrExprStmt, IrList, IrAssign, and IrCall
// (genia-2026's docs/architecture/core-ir-portability.md
// minimal_portable_node_families list has 29 entries; this slice
// implements exactly the subset its grammar can produce -- see
// AGENTS.md's "no shortcuts" rule: this is a real lowering step, not
// the parser AST reused as if it were IR).
//
// Wire shapes (matching hosts/python/ir_normalize.py's
// `_normalize_ir_node` exactly, so future slices' `lower` operation
// evidence is not built on a guess; only IrLiteral/IrVar/IrBinary/
// IrExprStmt are exercised by any pinned bootstrap case so far):
//   IrLiteral:  {"node": "IrLiteral", "value": <payload -- see below>}
//   IrVar:      {"node": "IrVar", "name": "<name>"}
//   IrBinary:   {"node": "IrBinary", "left": <ir>, "op": "PLUS"|"MINUS"|"STAR"|"SLASH"|"EQEQ",
//               "right": <ir>}
//   IrExprStmt: {"node": "IrExprStmt", "expr": <ir>}
//   IrList:     {"node": "IrList", "items": [<ir>, ...]}
//   IrAssign:   {"node": "IrAssign", "name": "<name>", "expr": <ir>}
//   IrCall:     {"node": "IrCall", "fn": <ir>, "args": [<ir>, ...]}
//
// IrLiteral's `value` payload varies by literal kind (R21 E21-2:
// "String/bool/nil IrLiteral payloads are unchanged" -- only numeric
// literals get the tagged payload; there is no separate Core IR node
// per literal kind):
//   integer: {"kind": "integer", "digits": "<canonical unsigned decimal text>"}
//   string:  the plain string value
//   bool:    the plain boolean value
//
// Every top-level program statement in this slice's grammar is an
// expression statement, so `lowering::lower_program` wraps each one in
// IrExprStmt (matching src/genia/lowering.py's `ExprStmt -> IrExprStmt`
// rule) -- EXCEPT IrAssign, which GENIA_RULES.md's "IrAssign placement"
// invariant places directly in IrBlock.exprs, never wrapped.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace genia::core_ir {

enum class Kind : std::uint8_t {
  Literal,
  Var,
  Binary,
  ExprStmt,
  List,
  Assign,
  Call,
};

enum class LiteralKind : std::uint8_t { Integer, String, Bool };

// Binary operator token names, matching genia-2026's parser token names
// (see src/genia/lowering.py: `IrBinary(lower(left), node.op, lower(right))`
// where `node.op` is the raw lexer token name, not the symbol).
enum class Op : std::uint8_t { Plus, Minus, Star, Slash, EqEq };

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
    case Op::EqEq:
      return "EQEQ";
  }
  return "";
}

struct Node {
  Kind kind = Kind::Literal;

  // Literal
  LiteralKind literal_kind = LiteralKind::Integer;
  std::string
      integer_digits;  // valid when literal_kind == Integer (R21 canonical unsigned decimal text)
  std::string string_value;  // valid when literal_kind == String
  bool bool_value = false;   // valid when literal_kind == Bool

  // Var: the referenced name. Assign: the target name. Call: the
  // callee name (this slice only supports calling a name directly).
  std::string name;

  // Binary
  Op op = Op::Plus;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

  // ExprStmt/Assign: the wrapped/assigned expression (reuses `left`).

  // List: element expressions. Call: argument expressions.
  std::vector<Node> items;

  static Node integer_literal(std::string digits) {
    Node n;
    n.kind = Kind::Literal;
    n.literal_kind = LiteralKind::Integer;
    n.integer_digits = std::move(digits);
    return n;
  }

  static Node string_literal(std::string value) {
    Node n;
    n.kind = Kind::Literal;
    n.literal_kind = LiteralKind::String;
    n.string_value = std::move(value);
    return n;
  }

  static Node bool_literal(bool value) {
    Node n;
    n.kind = Kind::Literal;
    n.literal_kind = LiteralKind::Bool;
    n.bool_value = value;
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

  static Node list(std::vector<Node> elements) {
    Node n;
    n.kind = Kind::List;
    n.items = std::move(elements);
    return n;
  }

  static Node assign(std::string target_name, Node value) {
    Node n;
    n.kind = Kind::Assign;
    n.name = std::move(target_name);
    n.left = std::make_shared<Node>(std::move(value));
    return n;
  }

  static Node call(std::string callee_name, std::vector<Node> args) {
    Node n;
    n.kind = Kind::Call;
    n.name = std::move(callee_name);
    n.items = std::move(args);
    return n;
  }
};

}  // namespace genia::core_ir
