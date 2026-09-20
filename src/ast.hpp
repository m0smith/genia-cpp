// Parser AST for the E24-2/E24-3 vertical slice: integer/string/boolean
// literals, list literals, bare-name references, assignment, function
// calls, and `+ - * / ==` binary expressions only.
//
// This is the host-local parser-AST layer (layer 2 of
// docs/architecture/core-ir-portability.md's four-layer model in
// genia-2026) -- NOT the portable Core IR boundary. It exists only to
// mirror the shape genia-2026's own `parse` category cases compare
// against (see hosts/python/parse_adapter.py's normalize_ast, which is
// this project's authoritative source for that wire shape). It is
// deliberately a tiny subset: any source outside this grammar must be
// rejected by the parser (see parser.hpp), never guessed at.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace genia::ast {

enum class Kind : std::uint8_t {
  Literal,
  StringLiteral,
  BoolLiteral,
  Var,
  Binary,
  List,
  Assign,
  Call,
};

struct Node {
  Kind kind = Kind::Literal;

  // Literal: unsigned decimal digit text (sign is never part of a
  // literal -- R21 keeps sign outside as a unary operator, which this
  // slice's grammar does not include at all).
  std::string integer_digits;

  // StringLiteral: decoded UTF-8 text (escape processing is minimal --
  // see parser.hpp).
  std::string string_value;

  // BoolLiteral
  bool bool_value = false;

  // Var: bare identifier name. Assign: the name being assigned. Call:
  // the callee name (this slice only supports calling a name directly,
  // never a general callable expression).
  std::string name;

  // Binary: symbolic operator ("+", "-", "*", "/", "==", matching the
  // parse AST projection's op_symbol_map spelling) plus operands.
  std::string op;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

  // List: element expressions. Call: argument expressions. Assign: the
  // single value expression (reuses `left`).
  std::vector<Node> items;

  static Node literal(std::string digits) {
    Node n;
    n.kind = Kind::Literal;
    n.integer_digits = std::move(digits);
    return n;
  }

  static Node string_literal(std::string value) {
    Node n;
    n.kind = Kind::StringLiteral;
    n.string_value = std::move(value);
    return n;
  }

  static Node bool_literal(bool value) {
    Node n;
    n.kind = Kind::BoolLiteral;
    n.bool_value = value;
    return n;
  }

  static Node var(std::string identifier) {
    Node n;
    n.kind = Kind::Var;
    n.name = std::move(identifier);
    return n;
  }

  static Node binary(std::string op_symbol, Node lhs, Node rhs) {
    Node n;
    n.kind = Kind::Binary;
    n.op = std::move(op_symbol);
    n.left = std::make_shared<Node>(std::move(lhs));
    n.right = std::make_shared<Node>(std::move(rhs));
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

// A program is a sequence of independent top-level expressions -- Genia
// programs are not newline- or separator-delimited at this level; one
// maximal expression is parsed, then the next token (if any) begins a
// new top-level expression (verified directly against genia-2026's
// reference host: `1 2 3` parses as three top-level Literal nodes, not
// one).
using Program = std::vector<Node>;

}  // namespace genia::ast
