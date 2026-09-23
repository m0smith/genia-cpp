// Parser AST for the E24-2..E24-4 vertical slice: integer/string/boolean
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
#include <utility>
#include <vector>

#include "pattern.hpp"

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
  // E24-4 additions. A pipeline (`|>`) is a Binary node with op "|>",
  // matching genia-2026's own AST layer (src/genia/ast_nodes.py's
  // `Binary(op="PIPE_FWD")`, flattened into a dedicated IR node only at
  // lowering time -- see lowering.hpp).
  Lambda,
  FuncDef,
  Map,
  Spread,
  // E24-6 (m0smith/genia-2026#958->959 R20 local open functions):
  // reuses FuncDef's case-body shape verbatim (name/case_patterns/
  // case_results) -- see docs/design/r20-open-functions-syntax-ir-
  // design.md section 4, which lowers OpenFuncDef 1:1 from the same
  // CaseClause list a case-body FuncDef already carries. This is its
  // own Kind (not a FuncDef flag) only because the parse-category wire
  // shape distinguishes `kind: "OpenFuncDef"` from `kind: "FuncDef"`
  // (hosts/python/parse_adapter.py's normalize_ast) -- runtime
  // evaluation is otherwise identical to a case-body FuncDef.
  OpenFuncDef,
  // E24-7 (R21 numeric source classification): a Decimal source literal
  // (`1.25`, `1e3`, `1.25e-2`, ...). Its own Kind, not a Literal flag,
  // because the two carry different payload fields (coefficient digits
  // + exponent vs a bare integer digit string) and project to different
  // wire shapes at both layers (parse-category plain numeric `value`
  // vs the R21 tagged `{kind:"decimal", coefficient, exponent}`
  // Core IR payload -- docs/design/r21-numeric-source-portable-
  // representation-contract.md section 4.2).
  DecimalLiteral,
  // E24-7: unary minus (`-<expr>`), genia-2026's real `Unary` AST node
  // (src/genia/ast_nodes.py) -- source sign is never part of a numeric
  // literal itself (R21 section 2: "-1.25 is unary minus applied to the
  // positive Decimal literal"), so negating a literal is this slice's
  // first real use of this node.
  Unary,
  Quote,
  QuasiQuote,
};

struct Node {
  Kind kind = Kind::Literal;

  // Literal: unsigned decimal digit text (sign is never part of a
  // literal -- R21 keeps sign outside as a unary operator, which this
  // slice's grammar does not include at all).
  std::string integer_digits;

  // DecimalLiteral: canonical unsigned coefficient digit text (no
  // leading zeros except a lone "0") and the paired exponent, per R21
  // section 4.2's canonicalization rule -- value is
  // `decimal_coefficient_digits * 10^decimal_exponent`. Sign is never
  // part of a Decimal literal either (same rule as Integer); a negative
  // Decimal source form is Unary(MINUS, DecimalLiteral(...)).
  std::string decimal_coefficient_digits;
  int64_t decimal_exponent = 0;

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
  // Unary: the same symbolic operator ("-") plus the single operand
  // (reuses `left`; `right` is unused).
  std::string op;
  std::shared_ptr<Node> left;
  std::shared_ptr<Node> right;

  // List: element expressions (an element may itself be Kind::Spread).
  // Call: argument expressions. Assign: the single value expression
  // (reuses `left`).
  std::vector<Node> items;

  // Lambda/FuncDef: one positional-parameter pattern per parameter.
  // Empty when `is_case_body` is true.
  std::vector<pattern::Pattern> params;

  // FuncDef only: the literal header parameter names (`name(a, b) = ...`),
  // kept independently of `params`/`case_patterns` because genia-2026's
  // real parse/lower wire shapes carry these plain names regardless of
  // whether the body is a case-dispatch expression (see
  // hosts/python/parse_adapter.py's FuncDef handler and
  // src/genia/lowering.py's IrFuncDef construction, both of which always
  // use the header's `node.params`, never anything derived from the
  // body).
  std::vector<std::string> header_param_names;

  // Lambda/FuncDef: true when the body is a local case/pattern-dispatch
  // expression (`pat -> result | pat -> result | ...`) rather than a
  // single ordinary expression -- see parser.hpp's case-clause grammar.
  bool is_case_body = false;
  std::vector<pattern::Pattern> case_patterns;
  std::vector<Node> case_results;

  // Lambda/FuncDef (ordinary body): the body expression (reuses
  // `left`). Spread: the spread expression (reuses `left`).

  // Map: key -> value-expression entries, in source order.
  std::vector<std::pair<std::string, Node>> map_entries;

  static Node literal(std::string digits) {
    Node n;
    n.kind = Kind::Literal;
    n.integer_digits = std::move(digits);
    return n;
  }

  static Node decimal_literal(std::string coefficient_digits, int64_t exponent) {
    Node n;
    n.kind = Kind::DecimalLiteral;
    n.decimal_coefficient_digits = std::move(coefficient_digits);
    n.decimal_exponent = exponent;
    return n;
  }

  static Node unary(std::string op_symbol, Node operand) {
    Node n;
    n.kind = Kind::Unary;
    n.op = std::move(op_symbol);
    n.left = std::make_shared<Node>(std::move(operand));
    return n;
  }

  static Node quote(Node inner, bool quasi) {
    Node n;
    n.kind = quasi ? Kind::QuasiQuote : Kind::Quote;
    n.left = std::make_shared<Node>(std::move(inner));
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

  // R20 local open function: `name`, in declaration order, one
  // IrCaseClause-equivalent pair per clause -- whether those clauses
  // came from a single grouped case-with-`|` body or from repeated
  // bare top-level statements merged by the parser (see parser.hpp's
  // `try_parse_open_related_toplevel`) is not observable here, matching
  // the design doc's "grouped/repeated equivalence is a lowering
  // property" rule (section 3).
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

  static Node map_literal(std::vector<std::pair<std::string, Node>> entries) {
    Node n;
    n.kind = Kind::Map;
    n.map_entries = std::move(entries);
    return n;
  }

  static Node spread(Node inner) {
    Node n;
    n.kind = Kind::Spread;
    n.left = std::make_shared<Node>(std::move(inner));
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
