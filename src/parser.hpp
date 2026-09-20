// Lexer + recursive-descent parser for the E24-2/E24-3 vertical slice.
//
// Grammar (deliberately minimal -- see genia-cpp AGENTS.md / E24-2/E24-3
// issue scope):
//
//   program   := statement*
//   statement := assignment | expr
//   assignment:= IDENT '=' expr
//   expr      := term (('+' | '-' | '==') term)*
//   term      := factor (('*' | '/') factor)*
//   factor    := INTEGER | STRING | 'true' | 'false' | list | call | IDENT
//   list      := '[' (expr (',' expr)*)? ']'
//   call      := IDENT '(' (expr (',' expr)*)? ')'
//
// No parens-as-grouping, no unary operators, no lambdas/pattern
// matching/user function definitions. Any token or arrangement outside
// this grammar makes the whole source unrepresentable in this slice;
// `parse_program` returns std::nullopt in that case, and callers must
// treat that as "unsupported" (a protocol-level adapter response),
// never as a language-level parse error -- this project does not yet
// implement genia-2026's actual diagnostic contract for real syntax
// errors (that is E24-5's job) and must not guess at it.
#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ast.hpp"

namespace genia::parser {

enum class TokenKind : std::uint8_t {
  Integer,
  String,
  Ident,
  Plus,
  Minus,
  Star,
  Slash,
  EqEq,
  Eq,
  LBracket,
  RBracket,
  LParen,
  RParen,
  Comma,
  End,
};

struct Token {
  TokenKind kind;
  std::string text;
};

// Tokenizes `source`. Returns std::nullopt if any character sequence
// does not belong to this slice's minimal token vocabulary, or if a
// string literal is unterminated or contains an unsupported escape.
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
      // '?' may terminate an identifier (e.g. `map_has?`), matching
      // genia-2026's real identifier grammar, but only directly
      // adjacent -- never as its own token.
      if (i < n && source[i] == '?') {
        ++i;
      }
      tokens.push_back({TokenKind::Ident, source.substr(start, i - start)});
      continue;
    }
    if (c == '"') {
      // Byte-transparent: each byte between the quotes is copied
      // through unexamined (multi-byte UTF-8 sequences pass correctly
      // since every continuation byte is >= 0x80, never '"' or '\\').
      // This is not yet genuine codepoint-aware UTF-8 handling (no
      // codepoint counting/indexing) -- see
      // docs/design/r24/native-primitive-inventory.md's "UTF-8
      // decode/code-point iteration" primitive; that becomes necessary
      // once a string-indexing/length function is in scope, which no
      // pinned E24-2/E24-3 evidence requires yet.
      size_t start = i;
      ++i;
      std::string decoded;
      bool closed = false;
      while (i < n) {
        const unsigned char sc = static_cast<unsigned char>(source[i]);
        if (sc == '"') {
          closed = true;
          ++i;
          break;
        }
        if (sc == '\\') {
          // No escape sequences are in this slice's grammar (no pinned
          // evidence needs any); a backslash makes the source
          // unsupported rather than guessing at escape semantics.
          return std::nullopt;
        }
        decoded.push_back(source[i]);
        ++i;
      }
      if (!closed) {
        return std::nullopt;
      }
      (void)start;
      tokens.push_back({TokenKind::String, decoded});
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
      case '[':
        tokens.push_back({TokenKind::LBracket, "["});
        ++i;
        continue;
      case ']':
        tokens.push_back({TokenKind::RBracket, "]"});
        ++i;
        continue;
      case '(':
        tokens.push_back({TokenKind::LParen, "("});
        ++i;
        continue;
      case ')':
        tokens.push_back({TokenKind::RParen, ")"});
        ++i;
        continue;
      case ',':
        tokens.push_back({TokenKind::Comma, ","});
        ++i;
        continue;
      case '=':
        if (i + 1 < n && source[i + 1] == '=') {
          tokens.push_back({TokenKind::EqEq, "=="});
          i += 2;
          continue;
        }
        tokens.push_back({TokenKind::Eq, "="});
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
      auto stmt = parse_statement();
      if (!stmt.has_value()) {
        return std::nullopt;
      }
      program.push_back(std::move(*stmt));
    }
    return program;
  }

 private:
  std::vector<Token> tokens_;
  size_t pos_ = 0;
  // Names seen as an assignment target earlier in this program.
  // Together with the "print" carve-out, this is the complete set of
  // identifiers this slice accepts as a bare Var reference -- see
  // parse_factor's comment for why an arbitrary identifier is not
  // accepted (genia-2026 keywords like `none` lower to their own Core
  // IR node, never IrVar). A name the user's own program assigns to is
  // definitionally not a language keyword being misparsed.
  std::set<std::string> assigned_names_;

  const Token& peek() const { return tokens_[pos_]; }
  const Token& peek_at(size_t offset) const {
    const size_t index = pos_ + offset;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
  }
  Token advance() { return tokens_[pos_++]; }

  std::optional<ast::Node> parse_statement() {
    if (peek().kind == TokenKind::Ident && peek_at(1).kind == TokenKind::Eq) {
      const std::string name = advance().text;
      advance();  // '='
      auto value = parse_expr();
      if (!value.has_value()) {
        return std::nullopt;
      }
      assigned_names_.insert(name);
      return ast::Node::assign(name, std::move(*value));
    }
    return parse_expr();
  }

  std::optional<ast::Node> parse_expr() {
    auto lhs = parse_term();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus ||
           peek().kind == TokenKind::EqEq) {
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

  // Parses a comma-separated sequence of expressions until `closing` is
  // reached (consuming `closing`). Returns std::nullopt on any
  // malformed arrangement (trailing comma, missing closer, etc.).
  std::optional<std::vector<ast::Node>> parse_expr_list(TokenKind closing) {
    std::vector<ast::Node> items;
    if (peek().kind == closing) {
      advance();
      return items;
    }
    while (true) {
      auto item = parse_expr();
      if (!item.has_value()) {
        return std::nullopt;
      }
      items.push_back(std::move(*item));
      if (peek().kind == TokenKind::Comma) {
        advance();
        continue;
      }
      if (peek().kind == closing) {
        advance();
        return items;
      }
      return std::nullopt;
    }
  }

  std::optional<ast::Node> parse_factor() {
    if (peek().kind == TokenKind::Integer) {
      return ast::Node::literal(advance().text);
    }
    if (peek().kind == TokenKind::String) {
      return ast::Node::string_literal(advance().text);
    }
    if (peek().kind == TokenKind::LBracket) {
      advance();
      auto items = parse_expr_list(TokenKind::RBracket);
      if (!items.has_value()) {
        return std::nullopt;
      }
      return ast::Node::list(std::move(*items));
    }
    if (peek().kind == TokenKind::Ident) {
      const std::string name = peek().text;
      if (name == "true") {
        advance();
        return ast::Node::bool_literal(true);
      }
      if (name == "false") {
        advance();
        return ast::Node::bool_literal(false);
      }
      if (peek_at(1).kind == TokenKind::LParen) {
        advance();  // name
        advance();  // '('
        auto args = parse_expr_list(TokenKind::RParen);
        if (!args.has_value()) {
          return std::nullopt;
        }
        return ast::Node::call(name, std::move(*args));
      }
      // See assigned_names_'s comment: "print" plus any name this same
      // program already assigned are the only bare identifiers this
      // slice accepts as a Var reference.
      if (name == "print" || assigned_names_.count(name) > 0) {
        advance();
        return ast::Node::var(name);
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
