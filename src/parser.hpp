// Lexer + recursive-descent parser for the E24-2 vertical slice.
//
// Grammar (deliberately minimal -- see genia-cpp AGENTS.md / E24-2 issue
// scope: integer literals, bare-name references, and `+ - * /` binary
// expressions only; no parens, no unary operators, no lists/maps/
// lambdas/pattern matching/strings):
//
//   program  := statement*
//   statement:= expr
//   expr     := term (('+' | '-') term)*
//   term     := factor (('*' | '/') factor)*
//   factor   := INTEGER | IDENT
//
// Any token outside {INTEGER, IDENT, '+', '-', '*', '/', whitespace} --
// or any malformed arrangement of those tokens (e.g. a trailing
// operator) -- makes the whole source unrepresentable in this slice's
// grammar. `parse_program` returns std::nullopt in that case; callers
// must treat that as "unsupported" (a protocol-level adapter response),
// never as a language-level parse error, since this project does not
// yet implement genia-2026's actual diagnostic contract for real syntax
// errors (that is E24-5's job) and must not guess at it.
#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ast.hpp"

namespace genia::parser {

enum class TokenKind : std::uint8_t { Integer, Ident, Plus, Minus, Star, Slash, End };

struct Token {
  TokenKind kind;
  std::string text;
};

// Tokenizes `source`. Returns std::nullopt if any character sequence
// does not belong to this slice's minimal token vocabulary.
inline std::optional<std::vector<Token>> tokenize(const std::string& source) {
  std::vector<Token> tokens;
  size_t i = 0;
  const size_t n = source.size();
  while (i < n) {
    const unsigned char c = static_cast<unsigned char>(source[i]);
    if (std::isspace(c)) {
      ++i;
      continue;
    }
    if (std::isdigit(c)) {
      size_t start = i;
      while (i < n && std::isdigit(static_cast<unsigned char>(source[i]))) {
        ++i;
      }
      // A digit run immediately followed (no whitespace) by '.' or
      // 'e'/'E' is genia-2026's Decimal source classification (R21: a
      // dot or exponent marker directly adjacent to digits, never two
      // independent tokens) -- e.g. "1e3" is one Decimal literal
      // attempt, not an Integer "1" followed by a bare identifier
      // "e3" (verified directly against genia-2026's
      // spec/parse/parse-r21-*.yaml evidence). Decimal literals are
      // out of this slice's grammar, so this must fail tokenization
      // (unsupported), never silently split into unrelated tokens.
      if (i < n && (source[i] == '.' || source[i] == 'e' || source[i] == 'E')) {
        return std::nullopt;
      }
      tokens.push_back({TokenKind::Integer, source.substr(start, i - start)});
      continue;
    }
    if (std::isalpha(c) || c == '_') {
      size_t start = i;
      while (i < n && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) {
        ++i;
      }
      tokens.push_back({TokenKind::Ident, source.substr(start, i - start)});
      continue;
    }
    switch (c) {
      case '+':
        tokens.push_back({TokenKind::Plus, "+"});
        ++i;
        continue;
      case '-':
        tokens.push_back({TokenKind::Minus, "-"});
        ++i;
        continue;
      case '*':
        tokens.push_back({TokenKind::Star, "*"});
        ++i;
        continue;
      case '/':
        tokens.push_back({TokenKind::Slash, "/"});
        ++i;
        continue;
      default:
        return std::nullopt;
    }
  }
  tokens.push_back({TokenKind::End, ""});
  return tokens;
}

class Parser {
 public:
  explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

  std::optional<ast::Program> parse_program() {
    ast::Program program;
    while (peek().kind != TokenKind::End) {
      auto expr = parse_expr();
      if (!expr.has_value()) {
        return std::nullopt;
      }
      program.push_back(std::move(*expr));
    }
    return program;
  }

 private:
  std::vector<Token> tokens_;
  size_t pos_ = 0;

  const Token& peek() const { return tokens_[pos_]; }
  Token advance() { return tokens_[pos_++]; }

  std::optional<ast::Node> parse_expr() {
    auto lhs = parse_term();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
      const std::string op = advance().text;
      auto rhs = parse_term();
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary(op, std::move(result), std::move(*rhs));
    }
    return result;
  }

  std::optional<ast::Node> parse_term() {
    auto lhs = parse_factor();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash) {
      const std::string op = advance().text;
      auto rhs = parse_factor();
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary(op, std::move(result), std::move(*rhs));
    }
    return result;
  }

  std::optional<ast::Node> parse_factor() {
    if (peek().kind == TokenKind::Integer) {
      return ast::Node::literal(advance().text);
    }
    if (peek().kind == TokenKind::Ident) {
      // Restricted to the one real, evidenced global name (see
      // global_env.hpp) rather than accepting any identifier-shaped
      // token: genia-2026's real grammar treats several bare words as
      // keywords with their own Core IR node (e.g. `none` lowers to
      // IrOptionNone, never IrVar -- see spec/ir/none-bare.yaml). This
      // slice has no keyword table, so accepting an arbitrary
      // identifier as an ordinary Var reference risks silently
      // misparsing a keyword. Accepting only "print" is a byte-exact,
      // evidence-backed carve-out, not a guess.
      if (peek().text == "print") {
        return ast::Node::var(advance().text);
      }
      return std::nullopt;
    }
    return std::nullopt;
  }
};

inline std::optional<ast::Program> parse_program(const std::string& source) {
  auto tokens = tokenize(source);
  if (!tokens.has_value()) {
    return std::nullopt;
  }
  Parser parser(std::move(*tokens));
  return parser.parse_program();
}

}  // namespace genia::parser
