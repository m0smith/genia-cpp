// Minimal portable Core IR nodes for the E24-2..E24-4 vertical slice:
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
#include <utility>
#include <vector>

#include "pattern.hpp"

namespace genia::core_ir {

enum class Kind : std::uint8_t {
  Literal,
  Var,
  Binary,
  ExprStmt,
  List,
  Block,
  Assign,
  Call,
  // E24-4 additions.
  Lambda,
  FuncDef,
  Map,
  Pipeline,
  Spread,
  // E24-6: portable IrOpenFuncDef (see ir_projection.hpp for the wire
  // shape). Reuses FuncDef's case-body fields verbatim; its own Kind
  // only because IrOpenFuncDef and IrFuncDef are distinct portable node
  // types (docs/design/r20-open-functions-syntax-ir-design.md section
  // 3), not because evaluation differs.
  OpenFuncDef,
  // E24-7: portable IrUnary (see ir_projection.hpp for the wire shape).
  Unary,
  Quote,
  QuasiQuote,
};

enum class LiteralKind : std::uint8_t { Integer, String, Bool, Decimal };

struct MapEntry;

// Binary operator token names, matching genia-2026's parser token names
// (see src/genia/lowering.py: `IrBinary(lower(left), node.op, lower(right))`
// where `node.op` is the raw lexer token name, not the symbol).
enum class Op : std::uint8_t { Plus, Minus, Star, Slash, Percent, EqEq, NotEq, Lt, Le, Gt, Ge };

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
    case Op::Percent:
      return "PERCENT";
    case Op::EqEq:
      return "EQEQ";
    case Op::NotEq:
      return "NE";
    case Op::Lt:
      return "LT";
    case Op::Le:
      return "LE";
    case Op::Gt:
      return "GT";
    case Op::Ge:
      return "GE";
  }
  return "";
}

struct Node {
  Node();
  ~Node();
  Node(const Node&);
  Node(Node&&) noexcept;
  Node& operator=(const Node&);
  Node& operator=(Node&&) noexcept;

  Kind kind = Kind::Literal;

  // Literal
  LiteralKind literal_kind = LiteralKind::Integer;
  std::string
      integer_digits;  // valid when literal_kind == Integer (R21 canonical unsigned decimal text)
  std::string string_value;  // valid when literal_kind == String
  bool bool_value = false;   // valid when literal_kind == Bool
  // valid when literal_kind == Decimal (R21 section 4.2's canonical
  // tagged payload: value is decimal_coefficient_digits * 10^decimal_exponent).
  std::string decimal_coefficient_digits;
  int64_t decimal_exponent = 0;

  // Var: the referenced name. Assign: the target name. Call: the
  // callee name (this slice only supports calling a name directly).
  std::string name;

  // Binary. Unary: the same `op` field plus the single operand (reuses
  // `left`; `right` is unused).
  Op op = Op::Plus;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

  // ExprStmt/Assign: the wrapped/assigned expression (reuses `left`).
  // Lambda/FuncDef (ordinary, non-case body): the body expression
  // (reuses `left`). Spread: the spread expression (reuses `left`).
  // Pipeline: the pipeline source (reuses `left`); `items` holds the
  // ordered stages.

  // List: element expressions. Call: argument expressions. Pipeline:
  // ordered stage expressions.
  std::vector<Node> items;

  // Lambda/FuncDef: one positional-parameter pattern per parameter
  // (e.g. a plain `x` parameter lowers to `IrPatBind("x")`). Empty when
  // `is_case_body` is true (a case-dispatch body binds nothing
  // positionally beforehand -- see pattern_match.hpp's `match`).
  std::vector<pattern::Pattern> params;

  // FuncDef only: the literal header parameter names -- see
  // ast.hpp's `header_param_names` for why this is independent of
  // `params`/`case_patterns` (genia-2026's real IrFuncDef.params wire
  // field always carries these, regardless of case-dispatch body).
  std::vector<std::string> header_param_names;

  // Lambda/FuncDef: true when the body is a local case/pattern-dispatch
  // expression (E24-4's `pattern_case_dispatch` category) rather than a
  // single ordinary expression. `case_patterns[i]` pairs with
  // `case_results[i]` as clause i's pattern and result expression,
  // tried in order (matching genia-2026's IrCase/IrCaseClause -- see
  // ir_projection.hpp for the wire shape this projects to).
  bool is_case_body = false;
  std::vector<pattern::Pattern> case_patterns;
  std::vector<Node> case_results;

  // Map: key -> value-expression entries, in source order (matching
  // genia-2026's IrMap.items; map-literal keys are always plain
  // strings, whether written as a bare identifier or a string literal
  // -- see src/genia/lowering.py's `_map_literal_key_name`).
  std::vector<MapEntry> map_entries;

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

  static Node decimal_literal(std::string coefficient_digits, int64_t exponent) {
    Node n;
    n.kind = Kind::Literal;
    n.literal_kind = LiteralKind::Decimal;
    n.decimal_coefficient_digits = std::move(coefficient_digits);
    n.decimal_exponent = exponent;
    return n;
  }

  static Node unary(Op op, Node operand) {
    Node n;
    n.kind = Kind::Unary;
    n.op = op;
    n.left = std::make_shared<Node>(std::move(operand));
    return n;
  }

  static Node quote(Node inner, bool quasi) {
    Node n;
    n.kind = quasi ? Kind::QuasiQuote : Kind::Quote;
    n.left = std::make_shared<Node>(std::move(inner));
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

  static Node block(std::vector<Node> expressions) {
    Node n;
    n.kind = Kind::Block;
    n.items = std::move(expressions);
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

  static Node lambda(std::vector<pattern::Pattern> parameter_patterns, Node body) {
    Node n;
    n.kind = Kind::Lambda;
    n.params = std::move(parameter_patterns);
    n.left = std::make_shared<Node>(std::move(body));
    return n;
  }

  static Node func_def(std::string func_name, std::vector<std::string> header_names,
                       std::vector<pattern::Pattern> parameter_patterns, Node body) {
    Node n;
    n.kind = Kind::FuncDef;
    n.name = std::move(func_name);
    n.header_param_names = std::move(header_names);
    n.params = std::move(parameter_patterns);
    n.left = std::make_shared<Node>(std::move(body));
    return n;
  }

  static Node func_def_case(std::string func_name, std::vector<std::string> header_names,
                            std::vector<pattern::Pattern> clause_patterns,
                            std::vector<Node> clause_results) {
    Node n;
    n.kind = Kind::FuncDef;
    n.name = std::move(func_name);
    n.header_param_names = std::move(header_names);
    n.is_case_body = true;
    n.case_patterns = std::move(clause_patterns);
    n.case_results = std::move(clause_results);
    return n;
  }

  static Node open_func_def(std::string func_name, std::vector<pattern::Pattern> clause_patterns,
                            std::vector<Node> clause_results) {
    Node n;
    n.kind = Kind::OpenFuncDef;
    n.name = std::move(func_name);
    n.is_case_body = true;
    n.case_patterns = std::move(clause_patterns);
    n.case_results = std::move(clause_results);
    return n;
  }

  static Node map_literal(std::vector<MapEntry> entries);

  static Node pipeline(Node source, std::vector<Node> stages) {
    Node n;
    n.kind = Kind::Pipeline;
    n.left = std::make_shared<Node>(std::move(source));
    n.items = std::move(stages);
    return n;
  }

  static Node spread(Node inner) {
    Node n;
    n.kind = Kind::Spread;
    n.left = std::make_shared<Node>(std::move(inner));
    return n;
  }
};

struct MapEntry {
  std::string key;
  Node value;
};

inline Node::Node() = default;
inline Node::~Node() = default;
inline Node::Node(const Node&) = default;
inline Node::Node(Node&&) noexcept = default;
inline Node& Node::operator=(const Node&) = default;
inline Node& Node::operator=(Node&&) noexcept = default;

inline Node Node::map_literal(std::vector<MapEntry> entries) {
  Node n;
  n.kind = Kind::Map;
  n.map_entries = std::move(entries);
  return n;
}

}  // namespace genia::core_ir
