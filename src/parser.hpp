// Lexer + recursive-descent parser for the E24-2..E24-4 vertical slice.
//
// Grammar (deliberately minimal -- see genia-cpp AGENTS.md / issue
// scope):
//
//   program    := statement*
//   statement  := func_def | assignment | pipeline_expr
//   func_def   := IDENT '(' (IDENT (',' IDENT)*)? ')' '=' func_body
//   func_body  := case_clauses | pipeline_expr
//   case_clauses := case_clause ('|' case_clause)*
//   case_clause  := (pattern_tuple | pattern_atom) '->' pipeline_expr
//   assignment := IDENT '=' pipeline_expr
//   pipeline_expr := expr ('|>' expr)*
//   expr       := term (('+' | '-' | '==') term)*
//   term       := factor (('*' | '/') factor)*
//   factor     := INTEGER | STRING | 'true' | 'false' | list | map_literal
//               | lambda | '(' pipeline_expr ')' | call | IDENT
//   lambda     := '(' (pattern_atom (',' pattern_atom)*)? ')' '->' expr
//   list       := '[' (list_item (',' list_item)*)? ']'
//   list_item  := '..' expr | expr
//   map_literal:= '{' ((IDENT | STRING) ':' expr (',' (IDENT | STRING) ':' expr)*)? '}'
//   call       := IDENT '(' (expr (',' expr)*)? ')'
//   pattern_tuple := '(' (pattern_atom (',' pattern_atom)*)? ')'
//   pattern_atom  := IDENT | '_' | '[' list_pattern_items? ']' | '{' map_pattern_items? '}'
//
// No general prefix/unary operators, no field access, no guard clauses
// (`? expr` before `->`), no glob/option/named patterns -- any token or
// arrangement outside this grammar makes the whole source
// unrepresentable in this slice; `parse_program` returns std::nullopt in
// that case, and callers must treat that as "unsupported" (a
// protocol-level adapter response), never as a language-level parse
// error -- this project does not yet implement genia-2026's actual
// diagnostic contract for real syntax errors (that is E24-5's job) and
// must not guess at it.
#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ast.hpp"
#include "pattern.hpp"

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
  LBrace,
  RBrace,
  Comma,
  Colon,
  Arrow,
  PipeFwd,
  Pipe,
  DotDot,
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
      // pinned evidence requires yet.
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
    if (c == '-' && i + 1 < n && source[i + 1] == '>') {
      tokens.push_back({TokenKind::Arrow, "->"});
      i += 2;
      continue;
    }
    if (c == '|' && i + 1 < n && source[i + 1] == '>') {
      tokens.push_back({TokenKind::PipeFwd, "|>"});
      i += 2;
      continue;
    }
    if (c == '.' && i + 1 < n && source[i + 1] == '.') {
      tokens.push_back({TokenKind::DotDot, ".."});
      i += 2;
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
      case '{':
        tokens.push_back({TokenKind::LBrace, "{"});
        ++i;
        continue;
      case '}':
        tokens.push_back({TokenKind::RBrace, "}"});
        ++i;
        continue;
      case ',':
        tokens.push_back({TokenKind::Comma, ","});
        ++i;
        continue;
      case ':':
        tokens.push_back({TokenKind::Colon, ":"});
        ++i;
        continue;
      case '|':
        tokens.push_back({TokenKind::Pipe, "|"});
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

// Identifiers genia-2026 treats as dedicated keywords/special forms,
// never an ordinary bindable name: `none`/`some`/`nil` lower to their
// own Core IR node (R21/R18, Option support this slice does not
// implement); `import` is its own top-level statement grammar
// production (src/genia/parser.py's `try_parse_import_stmt`), never an
// expression; `quote`/`delay`/`quasiquote`/`unquote`/`unquote_splicing`
// are call-shaped special forms that do NOT evaluate their argument the
// way an ordinary function call does (they lower to their own dedicated
// Core IR node -- IrQuote/IrDelay/IrQuasiQuote/IrUnquote/
// IrUnquoteSplicing -- per docs/architecture/core-ir-portability.md).
// None of these are implemented by this slice; accepting any of them as
// an ordinary Var reference or an ordinary Call would silently misparse
// them into a different, wrong program instead of honestly reporting
// the source as unsupported -- caught by running genia-2026's full
// shared spec corpus (not just this slice's own pinned evidence)
// against this build, e.g. `quote(x)` was parsing as an ordinary
// `Call("quote", [Var("x")])` and then failing on `x` being undefined,
// rather than being rejected outright.
inline bool is_reserved_keyword(const std::string& name) {
  return name == "none" || name == "some" || name == "nil" || name == "import" || name == "quote" ||
         name == "delay" || name == "quasiquote" || name == "unquote" || name == "unquote_splicing";
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

  const Token& peek() const { return tokens_[pos_]; }
  const Token& peek_at(size_t offset) const {
    const size_t index = pos_ + offset;
    return index < tokens_.size() ? tokens_[index] : tokens_.back();
  }
  Token advance() { return tokens_[pos_++]; }

  // ---- Statements --------------------------------------------------

  std::optional<ast::Node> parse_statement() {
    // A top-level statement starting with the literal identifier
    // "pattern" is always genia-2026's NamedPatternDef production
    // (`pattern name(param) = body`) -- src/genia/parser.py's
    // `try_parse_named_pattern_def` never falls through to ordinary
    // expression parsing here, it raises a SyntaxError on any other
    // shape. This slice does not implement NamedPatternDef, so this
    // whole source is unsupported; without this check, "pattern" would
    // otherwise parse as an ordinary bare Var, silently misparsing the
    // statement (caught by running genia-2026's full shared spec corpus
    // -- see is_reserved_keyword's comment for the sibling bug this
    // shares a root cause with).
    if (peek().kind == TokenKind::Ident && peek().text == "pattern") {
      return std::nullopt;
    }
    auto func_def = try_parse_func_def();
    if (func_def.has_value()) {
      return func_def;
    }
    if (peek().kind == TokenKind::Ident && peek_at(1).kind == TokenKind::Eq) {
      const std::string name = advance().text;
      advance();  // '='
      auto value = parse_pipeline_expr();
      if (!value.has_value()) {
        return std::nullopt;
      }
      return ast::Node::assign(name, std::move(*value));
    }
    return parse_pipeline_expr();
  }

  // `name(p1, p2, ...) = body` -- a real top-level grammar production,
  // distinct from an ordinary call-expression statement (`foo(1)`).
  // Backtracks (restoring `pos_`) and returns std::nullopt for anything
  // that is not this exact shape, so an ordinary call statement falls
  // through to `parse_pipeline_expr` unaffected.
  std::optional<ast::Node> try_parse_func_def() {
    if (!(peek().kind == TokenKind::Ident && peek_at(1).kind == TokenKind::LParen)) {
      return std::nullopt;
    }
    const size_t save = pos_;
    std::string func_name = advance().text;
    advance();  // '('
    std::vector<std::string> param_names;
    if (peek().kind != TokenKind::RParen) {
      while (true) {
        if (peek().kind != TokenKind::Ident) {
          pos_ = save;
          return std::nullopt;
        }
        param_names.push_back(advance().text);
        if (peek().kind == TokenKind::Comma) {
          advance();
          continue;
        }
        break;
      }
    }
    if (peek().kind != TokenKind::RParen) {
      pos_ = save;
      return std::nullopt;
    }
    advance();  // ')'
    if (peek().kind != TokenKind::Eq) {
      pos_ = save;
      return std::nullopt;
    }
    advance();  // '='

    if (looks_like_case_clause_start()) {
      auto clauses = parse_case_clauses();
      if (!clauses.has_value()) {
        return std::nullopt;
      }
      std::vector<pattern::Pattern> patterns;
      std::vector<ast::Node> results;
      patterns.reserve(clauses->size());
      results.reserve(clauses->size());
      for (auto& clause : *clauses) {
        patterns.push_back(std::move(clause.first));
        results.push_back(std::move(clause.second));
      }
      return ast::Node::func_def_case(func_name, param_names, std::move(patterns),
                                      std::move(results));
    }

    auto body = parse_pipeline_expr();
    if (!body.has_value()) {
      return std::nullopt;
    }
    std::vector<pattern::Pattern> patterns;
    patterns.reserve(param_names.size());
    for (auto& name : param_names) {
      patterns.push_back(pattern::Pattern::bind(name));
    }
    return ast::Node::func_def(func_name, param_names, std::move(patterns), std::move(*body));
  }

  // ---- Patterns ------------------------------------------------------

  std::optional<pattern::Pattern> parse_pattern_atom() {
    if (peek().kind == TokenKind::Ident) {
      const std::string text = peek().text;
      advance();
      if (text == "_") {
        return pattern::Pattern::wildcard();
      }
      return pattern::Pattern::bind(text);
    }
    if (peek().kind == TokenKind::LBracket) {
      advance();
      std::vector<pattern::Pattern> items;
      bool saw_rest = false;
      if (peek().kind != TokenKind::RBracket) {
        while (true) {
          if (saw_rest) {
            return std::nullopt;  // "..rest must be the final item in a list pattern"
          }
          if (peek().kind == TokenKind::DotDot) {
            advance();
            std::string rest_name;
            if (peek().kind == TokenKind::Ident) {
              rest_name = advance().text;
              if (rest_name == "_") {
                rest_name.clear();
              }
            }
            items.push_back(pattern::Pattern::rest(rest_name));
            saw_rest = true;
          } else {
            auto item = parse_pattern_atom();
            if (!item.has_value()) {
              return std::nullopt;
            }
            items.push_back(std::move(*item));
          }
          if (peek().kind == TokenKind::Comma) {
            advance();
            continue;
          }
          break;
        }
      }
      if (peek().kind != TokenKind::RBracket) {
        return std::nullopt;
      }
      advance();
      return pattern::Pattern::list(std::move(items));
    }
    if (peek().kind == TokenKind::LBrace) {
      advance();
      std::vector<std::pair<std::string, pattern::Pattern>> items;
      if (peek().kind != TokenKind::RBrace) {
        while (true) {
          if (peek().kind != TokenKind::Ident) {
            return std::nullopt;
          }
          std::string key = advance().text;
          pattern::Pattern value_pattern = pattern::Pattern::bind(key);
          if (peek().kind == TokenKind::Colon) {
            advance();
            auto inner = parse_pattern_atom();
            if (!inner.has_value()) {
              return std::nullopt;
            }
            value_pattern = std::move(*inner);
          }
          items.emplace_back(std::move(key), std::move(value_pattern));
          if (peek().kind == TokenKind::Comma) {
            advance();
            continue;
          }
          break;
        }
      }
      if (peek().kind != TokenKind::RBrace) {
        return std::nullopt;
      }
      advance();
      return pattern::Pattern::map(std::move(items));
    }
    return std::nullopt;
  }

  std::optional<pattern::Pattern> parse_pattern_tuple() {
    if (peek().kind != TokenKind::LParen) {
      return std::nullopt;
    }
    advance();
    std::vector<pattern::Pattern> items;
    if (peek().kind != TokenKind::RParen) {
      while (true) {
        auto item = parse_pattern_atom();
        if (!item.has_value()) {
          return std::nullopt;
        }
        items.push_back(std::move(*item));
        if (peek().kind == TokenKind::Comma) {
          advance();
          continue;
        }
        break;
      }
    }
    if (peek().kind != TokenKind::RParen) {
      return std::nullopt;
    }
    advance();
    return pattern::Pattern::tuple(std::move(items));
  }

  std::optional<pattern::Pattern> parse_case_clause_pattern() {
    if (peek().kind == TokenKind::LParen) {
      return parse_pattern_tuple();
    }
    return parse_pattern_atom();
  }

  // A pure lookahead: attempts to parse one case-clause pattern
  // followed by '->' without committing (always restores `pos_`).
  bool looks_like_case_clause_start() {
    const size_t save = pos_;
    auto pattern = parse_case_clause_pattern();
    const bool matched = pattern.has_value() && peek().kind == TokenKind::Arrow;
    pos_ = save;
    return matched;
  }

  std::optional<std::vector<std::pair<pattern::Pattern, ast::Node>>> parse_case_clauses() {
    std::vector<std::pair<pattern::Pattern, ast::Node>> clauses;
    while (true) {
      auto clause_pattern = parse_case_clause_pattern();
      if (!clause_pattern.has_value()) {
        return std::nullopt;
      }
      if (peek().kind != TokenKind::Arrow) {
        return std::nullopt;
      }
      advance();
      auto result = parse_pipeline_expr();
      if (!result.has_value()) {
        return std::nullopt;
      }
      clauses.emplace_back(std::move(*clause_pattern), std::move(*result));
      if (peek().kind == TokenKind::Pipe) {
        advance();
        continue;
      }
      break;
    }
    return clauses;
  }

  // ---- Expressions -----------------------------------------------

  std::optional<ast::Node> parse_pipeline_expr() {
    auto left = parse_expr();
    if (!left.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*left);
    while (peek().kind == TokenKind::PipeFwd) {
      advance();
      auto right = parse_expr();
      if (!right.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary("|>", std::move(result), std::move(*right));
    }
    return result;
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

  // Wraps `parse_factor` with a check for a leftover, unconsumed '('
  // immediately following a factor that was NOT an identifier-based
  // call (Call already consumes its own trailing '(' args ')' inside
  // `parse_factor`, so this can only fire for a Literal/List/
  // MapLiteral/Lambda/general-grouping result). genia-2026's real
  // grammar treats that as a general postfix call application (e.g.
  // `config_view(...)("PORT")`, or an immediately-invoked lambda
  // literal `((x) -> x)(1)`) -- a real feature this slice does not
  // implement. Refusing here (returning std::nullopt, which propagates
  // all the way up through parse_program) keeps this honestly
  // unsupported; without this check, the caller would otherwise treat
  // the leftover '(...)' as the START of an unrelated new top-level
  // statement, silently misparsing the source into a different, wrong
  // program instead -- caught by running genia-2026's full shared spec
  // corpus against this build (see is_reserved_keyword's comment for
  // the sibling bug this shares a root cause with).
  std::optional<ast::Node> parse_factor_checked() {
    auto factor = parse_factor();
    if (!factor.has_value()) {
      return std::nullopt;
    }
    if (peek().kind == TokenKind::LParen) {
      return std::nullopt;
    }
    return factor;
  }

  std::optional<ast::Node> parse_term() {
    auto lhs = parse_factor_checked();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash) {
      const std::string op = advance().text;
      auto rhs = parse_factor_checked();
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary(op, std::move(result), std::move(*rhs));
    }
    return result;
  }

  // Parses a comma-separated sequence of call-argument expressions
  // until `closing` is reached (consuming `closing`). Never accepts a
  // spread item -- spread is only meaningful inside a list literal (see
  // `parse_list_items`); accepting it here would let a spread argument
  // silently pass the whole spread list as one ordinary argument value
  // instead of splicing it (a wrong result, not an honest "unsupported"
  // one), so it is simply not part of this grammar production.
  std::optional<std::vector<ast::Node>> parse_call_args(TokenKind closing) {
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

  // List-literal items, which (unlike call arguments) may be spread
  // (`..expr`) -- needed by this project's own embedded prelude source
  // (see global_env.hpp's `map_acc`, which builds its accumulator via
  // `[..acc, apply_raw(f, [x])]`).
  std::optional<std::vector<ast::Node>> parse_list_items() {
    std::vector<ast::Node> items;
    if (peek().kind == TokenKind::RBracket) {
      advance();
      return items;
    }
    while (true) {
      if (peek().kind == TokenKind::DotDot) {
        advance();
        auto inner = parse_expr();
        if (!inner.has_value()) {
          return std::nullopt;
        }
        items.push_back(ast::Node::spread(std::move(*inner)));
      } else {
        auto item = parse_expr();
        if (!item.has_value()) {
          return std::nullopt;
        }
        items.push_back(std::move(*item));
      }
      if (peek().kind == TokenKind::Comma) {
        advance();
        continue;
      }
      if (peek().kind == TokenKind::RBracket) {
        advance();
        return items;
      }
      return std::nullopt;
    }
  }

  std::optional<ast::Node> parse_map_literal() {
    advance();  // '{'
    std::vector<std::pair<std::string, ast::Node>> entries;
    if (peek().kind != TokenKind::RBrace) {
      while (true) {
        if (peek().kind != TokenKind::Ident && peek().kind != TokenKind::String) {
          return std::nullopt;
        }
        std::string key = advance().text;
        if (peek().kind != TokenKind::Colon) {
          return std::nullopt;
        }
        advance();
        auto value = parse_expr();
        if (!value.has_value()) {
          return std::nullopt;
        }
        entries.emplace_back(std::move(key), std::move(*value));
        if (peek().kind == TokenKind::Comma) {
          advance();
          continue;
        }
        break;
      }
    }
    if (peek().kind != TokenKind::RBrace) {
      return std::nullopt;
    }
    advance();
    return ast::Node::map_literal(std::move(entries));
  }

  // Tries `(pattern_atom, ...) -> expr` (a lambda); on any mismatch,
  // restores `pos_` and returns std::nullopt so the caller can fall back
  // to ordinary parenthesized grouping.
  std::optional<ast::Node> try_parse_lambda() {
    const size_t save = pos_;
    advance();  // '('
    std::vector<pattern::Pattern> params;
    bool ok = true;
    if (peek().kind != TokenKind::RParen) {
      while (true) {
        auto item = parse_pattern_atom();
        if (!item.has_value()) {
          ok = false;
          break;
        }
        params.push_back(std::move(*item));
        if (peek().kind == TokenKind::Comma) {
          advance();
          continue;
        }
        break;
      }
    }
    if (ok && peek().kind == TokenKind::RParen) {
      advance();
      if (peek().kind == TokenKind::Arrow) {
        advance();
        auto body = parse_expr();
        if (body.has_value()) {
          return ast::Node::lambda(std::move(params), std::move(*body));
        }
      }
    }
    pos_ = save;
    return std::nullopt;
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
      auto items = parse_list_items();
      if (!items.has_value()) {
        return std::nullopt;
      }
      return ast::Node::list(std::move(*items));
    }
    if (peek().kind == TokenKind::LBrace) {
      return parse_map_literal();
    }
    if (peek().kind == TokenKind::LParen) {
      auto lambda = try_parse_lambda();
      if (lambda.has_value()) {
        return lambda;
      }
      advance();  // '('
      auto inner = parse_pipeline_expr();
      if (!inner.has_value()) {
        return std::nullopt;
      }
      if (peek().kind != TokenKind::RParen) {
        return std::nullopt;
      }
      advance();
      return inner;
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
      if (is_reserved_keyword(name)) {
        return std::nullopt;
      }
      if (peek_at(1).kind == TokenKind::LParen) {
        advance();  // name
        advance();  // '('
        auto args = parse_call_args(TokenKind::RParen);
        if (!args.has_value()) {
          return std::nullopt;
        }
        return ast::Node::call(name, std::move(*args));
      }
      // Any other bare identifier is an ordinary Var reference; whether
      // it is actually bound is a runtime concern (see evaluator.hpp's
      // undefined-name handling), never a parse-time restriction --
      // matching genia-2026's real grammar, where identifier acceptance
      // does not depend on binding status.
      advance();
      return ast::Node::var(name);
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
