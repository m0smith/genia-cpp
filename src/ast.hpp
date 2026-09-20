// Parser AST for the E24-2 vertical slice: integer literals, bare-name
// references, and `+ - * /` binary expressions only.
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

enum class Kind : std::uint8_t { Literal, Var, Binary };

struct Node {
  Kind kind = Kind::Literal;

  // Literal: unsigned decimal digit text (sign is never part of a
  // literal -- R21 keeps sign outside as a unary operator, which this
  // slice's grammar does not include at all).
  std::string integer_digits;

  // Var: bare identifier name.
  std::string name;

  // Binary: symbolic operator ("+", "-", "*", "/", matching the parse
  // AST projection's op_symbol_map spelling) plus operands.
  std::string op;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

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

  static Node binary(std::string op_symbol, Node lhs, Node rhs) {
    Node n;
    n.kind = Kind::Binary;
    n.op = std::move(op_symbol);
    n.left = std::make_shared<Node>(std::move(lhs));
    n.right = std::make_shared<Node>(std::move(rhs));
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
