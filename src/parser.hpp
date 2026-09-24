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
//   expr       := equality
//   equality   := comparison (('==' | '!=') comparison)*
//   comparison := additive (('<' | '<=' | '>' | '>=') additive)*
//   additive   := term (('+' | '-') term)*
//   term       := factor (('*' | '/' | '%') factor)*
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
#include "bignum.hpp"
#include "pattern.hpp"

namespace genia::parser {

enum class TokenKind : std::uint8_t {
  Integer,
  Decimal,
  String,
  Ident,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  EqEq,
  NotEq,
  Lt,
  Le,
  Gt,
  Ge,
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
      // R21 numeric source classification (docs/design/r21-numeric-
      // source-portable-representation-contract.md section 2):
      //   integer          := DIGIT+
      //   decimal-dotted   := DIGIT+ "." DIGIT+
      //   decimal-exp      := DIGIT+ exponent
      //   decimal-dot-exp  := DIGIT+ "." DIGIT+ exponent
      //   exponent         := ("e"|"E") ("+"|"-")? DIGIT+
      // A dot not immediately followed by a digit is a trailing-dot
      // form ("5."), not part of this literal -- the dot is left
      // unconsumed for the main loop, which has no token for a bare
      // '.' and correctly fails tokenization (matching genia-2026's
      // real "Unexpected character" rejection without reproducing that
      // diagnostic text). A leading-dot form (".5") never reaches this
      // branch at all (isdigit(c) is false for '.'), so it fails the
      // same way. An exponent marker with no digit after it (optional
      // sign included) is a malformed exponent -- e.g. "1e", "1e+" --
      // and fails tokenization outright, never guessed at.
      bool is_decimal = false;
      if (i < n && source[i] == '.' && i + 1 < n &&
          std::isdigit(static_cast<unsigned char>(source[i + 1]))) {
        is_decimal = true;
        ++i;  // '.'
        while (i < n && std::isdigit(static_cast<unsigned char>(source[i]))) {
          ++i;
        }
      }
      if (i < n && (source[i] == 'e' || source[i] == 'E')) {
        ++i;  // 'e'/'E'
        if (i < n && (source[i] == '+' || source[i] == '-')) {
          ++i;
        }
        const size_t exponent_digits_start = i;
        while (i < n && std::isdigit(static_cast<unsigned char>(source[i]))) {
          ++i;
        }
        if (i == exponent_digits_start) {
          return std::nullopt;
        }
        is_decimal = true;
      }
      tokens.push_back(
          {is_decimal ? TokenKind::Decimal : TokenKind::Integer, source.substr(start, i - start)});
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
      case '%':
        tokens.push_back({TokenKind::Percent, "%"});
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
      case '!':
        // `!=` only -- this slice does not implement bare `!` (boolean
        // not, genia-2026's own BANG token), no pinned evidence needs
        // it, so a lone `!` is genuinely unsupported rather than
        // guessed at.
        if (i + 1 < n && source[i + 1] == '=') {
          tokens.push_back({TokenKind::NotEq, "!="});
          i += 2;
          continue;
        }
        return std::nullopt;
      case '<':
        if (i + 1 < n && source[i + 1] == '=') {
          tokens.push_back({TokenKind::Le, "<="});
          i += 2;
          continue;
        }
        tokens.push_back({TokenKind::Lt, "<"});
        ++i;
        continue;
      case '>':
        if (i + 1 < n && source[i + 1] == '=') {
          tokens.push_back({TokenKind::Ge, ">="});
          i += 2;
          continue;
        }
        tokens.push_back({TokenKind::Gt, ">"});
        ++i;
        continue;
      default:
        return std::nullopt;
    }
  }
  tokens.push_back({TokenKind::End, ""});
  return tokens;
}

// One canonical R21 Decimal literal payload: value is
// `coefficient_digits * 10^exponent` (section 4.2's tagged Core IR
// payload shape, computed once here so both the parse-category and
// lower-category wire projections derive from the identical canonical
// form -- exactly how an Integer literal's `integer_digits` is already
// shared between the two).
struct DecimalLiteralValue {
  std::string coefficient_digits;
  int64_t exponent = 0;
};

// Canonicalizes a raw Decimal token's source text (already validated by
// the tokenizer to match `DIGIT+ ("." DIGIT+)? (("e"|"E") ("+"|"-")?
// DIGIT+)?`) per R21 section 4.2 / R22 section 2's canonicalization
// rule: zero -> ("0", 0); otherwise strip every trailing base-10 zero
// from the absolute coefficient and increase the exponent by the count
// removed. Returns std::nullopt only if the exponent text's magnitude
// does not fit an int64_t (adversarially large source text -- no pinned
// evidence needs one this large; this is the same overflow-safety
// convention as ast_projection.hpp's `digits_to_safe_int64`).
inline std::optional<DecimalLiteralValue> canonicalize_decimal_literal(const std::string& raw) {
  size_t i = 0;
  const size_t n = raw.size();
  const size_t int_start = i;
  while (i < n && std::isdigit(static_cast<unsigned char>(raw[i]))) {
    ++i;
  }
  const std::string int_part = raw.substr(int_start, i - int_start);
  std::string frac_part;
  if (i < n && raw[i] == '.') {
    ++i;
    const size_t frac_start = i;
    while (i < n && std::isdigit(static_cast<unsigned char>(raw[i]))) {
      ++i;
    }
    frac_part = raw.substr(frac_start, i - frac_start);
  }
  int64_t exponent_from_marker = 0;
  if (i < n && (raw[i] == 'e' || raw[i] == 'E')) {
    ++i;
    bool negative_exponent = false;
    if (i < n && (raw[i] == '+' || raw[i] == '-')) {
      negative_exponent = raw[i] == '-';
      ++i;
    }
    const size_t exp_digits_start = i;
    while (i < n && std::isdigit(static_cast<unsigned char>(raw[i]))) {
      ++i;
    }
    const std::string exp_digits = raw.substr(exp_digits_start, i - exp_digits_start);
    try {
      size_t consumed = 0;
      const long long parsed = std::stoll(exp_digits, &consumed);
      if (consumed != exp_digits.size()) {
        return std::nullopt;
      }
      exponent_from_marker = negative_exponent ? -parsed : parsed;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }
  const std::string raw_digits = int_part + frac_part;
  auto magnitude = bignum::Integer::from_unsigned_decimal(raw_digits);
  if (!magnitude.has_value()) {
    return std::nullopt;
  }
  // Moving the decimal point right past `frac_part`'s digits requires
  // subtracting its length from the exponent the source's own marker
  // contributed. `frac_part.size()` is bounded by the source text's own
  // length, physically nowhere near int64_t's range, so this never
  // overflows in practice.
  const int64_t exponent = exponent_from_marker - static_cast<int64_t>(frac_part.size());
  if (magnitude->is_zero()) {
    return DecimalLiteralValue{"0", 0};
  }
  std::string digits = magnitude->to_decimal_string();  // canonical, no leading zeros, no sign
  int64_t trimmed_exponent = exponent;
  size_t keep = digits.size();
  while (keep > 1 && digits[keep - 1] == '0') {
    --keep;
    ++trimmed_exponent;
  }
  digits.resize(keep);
  return DecimalLiteralValue{std::move(digits), trimmed_exponent};
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
      // R20: a contiguous run of top-level clauses for the same open
      // interface merges into one AST node so grouped and repeated
      // local clause syntax normalize identically (design doc section
      // 4's "merge into one IrOpenFuncDef.clauses list happens during
      // AST lowering ... before any node becomes a separate top-level
      // IR statement" -- this project's AST layer is where genia-2026's
      // own parser performs that merge, per its `_merge_open_toplevel`,
      // so this mirrors that exactly). Only the immediately preceding
      // top-level node is eligible: any intervening statement closes
      // the run for further un-annotated repetition.
      if (!program.empty() && stmt->kind == ast::Kind::OpenFuncDef &&
          program.back().kind == ast::Kind::OpenFuncDef && program.back().name == stmt->name) {
        for (size_t i = 0; i < stmt->case_patterns.size(); ++i) {
          program.back().case_patterns.push_back(std::move(stmt->case_patterns[i]));
          program.back().case_results.push_back(std::move(stmt->case_results[i]));
        }
        continue;
      }
      program.push_back(std::move(*stmt));
    }
    return program;
  }

 private:
  std::vector<Token> tokens_;
  size_t pos_ = 0;

  // E24-6: names declared `open` earlier in this parse, for
  // redeclaration detection and for recognizing a bare repeated clause
  // as belonging to that interface -- mirrors genia-2026's real
  // src/genia/parser.py `Parser._open_names` set exactly (verified
  // directly against that source). Cross-module `extend`/`use` targets
  // are out of this slice's scope (require `multi_file_eval`), so only
  // local `open` names are tracked here.
  std::vector<std::string> open_names_;

  bool is_open_name(const std::string& name) const {
    for (const auto& existing : open_names_) {
      if (existing == name) {
        return true;
      }
    }
    return false;
  }

  // E24-5 (m0smith/genia-2026#959) diagnostic-normalization hardening:
  // recursive-descent nesting (parenthesized grouping, lambdas, nested
  // list/map literals or patterns) recurses through several mutually
  // recursive parse functions per source-level nesting level. A C++
  // stack overflow is undefined behavior, uncatchable by any try/catch,
  // so pathologically deep (or simply very deep) nesting would
  // otherwise segfault the whole adapter process -- verified
  // empirically: a few thousand levels of nested parens reliably
  // segfaults this parser without this guard. This limit is deliberately
  // far below that measured crash threshold and changes no observable
  // behavior for any source shallower than it.
  static constexpr int kMaxNestingDepth = 300;
  int nesting_depth_ = 0;

  struct NestingGuard {
    explicit NestingGuard(int& depth) : depth_(depth) { ++depth_; }
    ~NestingGuard() { --depth_; }
    NestingGuard(const NestingGuard&) = delete;
    NestingGuard& operator=(const NestingGuard&) = delete;
    NestingGuard(NestingGuard&&) = delete;
    NestingGuard& operator=(NestingGuard&&) = delete;
    int& depth_;
  };

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
    // R20 cross-module `extend`/`use` require `multi_file_eval`
    // (docs/design/r24/capability-floor.json's
    // generic_manifest_optional_capabilities_explicitly_out_of_r24_scope),
    // which is out of this slice's scope entirely -- no parsing support
    // is implemented for either. genia-2026's real parser sometimes
    // backtracks "extend"/"use" back to an ordinary bare identifier
    // when the full statement shape isn't present (src/genia/parser.py's
    // `try_parse_open_related_toplevel`); this slice deliberately does
    // not replicate that nuance (real SyntaxError conditions exist deep
    // inside those productions this slice has no diagnostic for), and
    // unconditionally rejects the whole program instead of risking a
    // silent misparse into a different, wrong AST -- the same
    // ambiguity-stop rule as the "pattern" keyword just above.
    if (peek().kind == TokenKind::Ident && (peek().text == "extend" || peek().text == "use")) {
      return std::nullopt;
    }
    auto open_related = try_parse_open_related_toplevel();
    if (open_related.has_value()) {
      return open_related;
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

  // ---- R20 open functions (local scope only) ------------------------
  //
  // Recognizes exactly the two top-level productions
  // docs/design/r20-open-functions-syntax-ir-design.md section 2.1
  // describes for LOCAL clause syntax: `open name(<pattern>, ...) =
  // <body>` (the first clause of a new open interface) and a bare
  // `name(<pattern>, ...) = <body>` for a name already declared open
  // (a repeated clause, merged into the same interface only when it
  // immediately follows -- see `parse_program`'s merge loop below).
  // Cross-module `extend`/`use` require `multi_file_eval`
  // (docs/design/r24/capability-floor.json's
  // generic_manifest_optional_capabilities_explicitly_out_of_r24_scope),
  // so they are out of this slice's scope and not recognized at all --
  // `extend`/`use` remain ordinary identifiers, falling through to
  // ordinary expression parsing exactly like any other non-keyword.
  //
  // Mirrors src/genia/parser.py's `try_parse_open_related_toplevel`/
  // `_parse_open_pattern_clause`/`_header_looks_like_open_clause`
  // exactly for these two shapes (verified directly against that
  // source). Once a header has been confirmed via lookahead to be an
  // open/repeated clause, any further parse failure is a genuine
  // failure of this slice's supported grammar -- it is never
  // backtracked to be reinterpreted as some other statement shape,
  // matching `try_parse_func_def`'s own post-'=' commit behavior above.
  std::optional<ast::Node> try_parse_open_related_toplevel() {
    if (peek().kind == TokenKind::Ident && peek().text == "open") {
      const size_t save = pos_;
      advance();  // 'open'
      if (!(peek().kind == TokenKind::Ident && peek_at(1).kind == TokenKind::LParen)) {
        pos_ = save;
        return std::nullopt;
      }
      return parse_open_pattern_clause(/*is_open_decl=*/true);
    }
    if (peek().kind == TokenKind::Ident && peek_at(1).kind == TokenKind::LParen &&
        is_open_name(peek().text)) {
      if (!header_looks_like_open_clause()) {
        return std::nullopt;
      }
      return parse_open_pattern_clause(/*is_open_decl=*/false);
    }
    return std::nullopt;
  }

  // Speculative lookahead, always restoring `pos_`: confirms
  // `name(...)` is followed by `=` before committing to clause parsing,
  // so an ordinary call statement for an open name (e.g. `gcd(48, 18)`)
  // is never misparsed as a failed repeated-clause attempt. This slice
  // does not implement the `? guard` syntax (no pinned evidence needs
  // it), so unlike the reference parser's `? | = | {` check, only `=`
  // is accepted here.
  bool header_looks_like_open_clause() {
    const size_t save = pos_;
    advance();  // name
    advance();  // '('
    int depth = 1;
    while (depth > 0) {
      if (peek().kind == TokenKind::End) {
        pos_ = save;
        return false;
      }
      if (peek().kind == TokenKind::LParen) {
        ++depth;
      } else if (peek().kind == TokenKind::RParen) {
        --depth;
      }
      advance();
    }
    const bool result = peek().kind == TokenKind::Eq;
    pos_ = save;
    return result;
  }

  std::optional<ast::Node> parse_open_pattern_clause(bool is_open_decl) {
    const std::string name = advance().text;
    if (is_open_decl) {
      if (is_open_name(name)) {
        // open-function-redeclaration: this slice implements no
        // diagnostic for it (no pinned evidence needs one), so the
        // whole program is honestly unsupported rather than silently
        // reinterpreted as a second, independent interface.
        return std::nullopt;
      }
      open_names_.push_back(name);
    }
    auto clauses = parse_open_clause_list();
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
    return ast::Node::open_func_def(name, std::move(patterns), std::move(results));
  }

  // Shared header parsing for an open/repeated clause: `(<pattern>,
  // ...) = body`, where `<pattern>` reuses the existing per-argument
  // pattern grammar (`parse_pattern_atom`) verbatim -- R20 adds no new
  // pattern grammar (design doc section 1). A grouped case-with-`|`
  // body flattens into one clause per arm, exactly like
  // `try_parse_func_def`'s existing grouped-clause handling above, so
  // grouped and repeated local clause syntax normalize to the identical
  // ordered clause list (design doc section 3).
  std::optional<std::vector<std::pair<pattern::Pattern, ast::Node>>> parse_open_clause_list() {
    if (peek().kind != TokenKind::LParen) {
      return std::nullopt;
    }
    advance();  // '('
    std::vector<pattern::Pattern> header_patterns;
    if (peek().kind != TokenKind::RParen) {
      while (true) {
        auto item = parse_pattern_atom();
        if (!item.has_value()) {
          return std::nullopt;
        }
        header_patterns.push_back(std::move(*item));
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
    advance();  // ')'
    if (peek().kind != TokenKind::Eq) {
      return std::nullopt;
    }
    advance();  // '='

    if (looks_like_case_clause_start()) {
      return parse_case_clauses();
    }

    auto body = parse_pipeline_expr();
    if (!body.has_value()) {
      return std::nullopt;
    }
    std::vector<std::pair<pattern::Pattern, ast::Node>> clauses;
    clauses.emplace_back(pattern::Pattern::tuple(std::move(header_patterns)), std::move(*body));
    return clauses;
  }

  // ---- Patterns ------------------------------------------------------

  std::optional<pattern::Pattern> parse_pattern_atom() {
    NestingGuard guard(nesting_depth_);
    if (nesting_depth_ > kMaxNestingDepth) {
      return std::nullopt;
    }
    if (peek().kind == TokenKind::Integer) {
      return pattern::Pattern::integer_literal(advance().text);
    }
    if (peek().kind == TokenKind::Decimal) {
      auto canonical = canonicalize_decimal_literal(advance().text);
      if (!canonical.has_value()) return std::nullopt;
      return pattern::Pattern::decimal_literal(std::move(canonical->coefficient_digits),
                                               canonical->exponent);
    }
    if (peek().kind == TokenKind::Ident) {
      const std::string text = peek().text;
      advance();
      if (text == "_") {
        return pattern::Pattern::wildcard();
      }
      if (text == "err" && peek().kind == TokenKind::LParen) {
        advance();
        auto reason = parse_pattern_atom();
        if (!reason.has_value() || peek().kind != TokenKind::Comma) return std::nullopt;
        advance();
        auto context = parse_pattern_atom();
        if (!context.has_value() || peek().kind != TokenKind::RParen) return std::nullopt;
        advance();
        return pattern::Pattern::err(std::move(*reason), std::move(*context));
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
      std::vector<pattern::PatternMapEntry> items;
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
          items.push_back(pattern::PatternMapEntry{std::move(key), std::move(value_pattern)});
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

  // Precedence-climbing chain matching genia-2026's real
  // src/genia/parser.py PRECEDENCE table exactly: EQEQ/NE=30 <
  // LT/LE/GT/GE=40 < PLUS/MINUS=50 < STAR/SLASH/PERCENT=60 (higher binds
  // tighter). Prior to this slice, `parse_expr` flattened PLUS/MINUS and
  // EQEQ/NOTEQ into one level -- a latent conformance bug relative to
  // that table (e.g. `1 == 2 + 3` must group as `1 == (2 + 3)`, not
  // `(1 == 2) + 3`) that happened to never surface as a silently wrong
  // *value* (a Boolean/Integer arithmetic mix like `(1 == 2) + 3` is
  // itself unsupported by this slice's exact-family-only arithmetic, so
  // the bug only ever manifested as an honest `unsupported`, never a
  // wrong `ok` result) -- fixed here while adding the new comparison
  // tier, verified directly against `src/genia/parser.py`'s own table,
  // not guessed at.
  std::optional<ast::Node> parse_expr() { return parse_equality(); }

  std::optional<ast::Node> parse_equality() {
    auto lhs = parse_comparison();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::EqEq || peek().kind == TokenKind::NotEq) {
      const std::string op = advance().text;
      auto rhs = parse_comparison();
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary(op, std::move(result), std::move(*rhs));
    }
    return result;
  }

  std::optional<ast::Node> parse_comparison() {
    auto lhs = parse_additive();
    if (!lhs.has_value()) {
      return std::nullopt;
    }
    ast::Node result = std::move(*lhs);
    while (peek().kind == TokenKind::Lt || peek().kind == TokenKind::Le ||
           peek().kind == TokenKind::Gt || peek().kind == TokenKind::Ge) {
      const std::string op = advance().text;
      auto rhs = parse_additive();
      if (!rhs.has_value()) {
        return std::nullopt;
      }
      result = ast::Node::binary(op, std::move(result), std::move(*rhs));
    }
    return result;
  }

  std::optional<ast::Node> parse_additive() {
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
    while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash ||
           peek().kind == TokenKind::Percent) {
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
        if (peek().kind == closing) {
          advance();
          return items;
        }
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
        if (peek().kind == TokenKind::RBracket) {
          advance();
          return items;
        }
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
    std::vector<ast::MapEntry> entries;
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
        entries.push_back(ast::MapEntry{std::move(key), std::move(*value)});
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
    NestingGuard guard(nesting_depth_);
    if (nesting_depth_ > kMaxNestingDepth) {
      return std::nullopt;
    }
    if (peek().kind == TokenKind::Minus) {
      // Unary minus (R21 section 2: source sign is never part of a
      // numeric literal itself -- "-1.25 is unary minus applied to the
      // positive Decimal literal"). Binds to the immediately following
      // factor, tighter than `* / %`, so `-2 * 3` is `(-2) * 3` and
      // `--5` is `-(-5)`.
      advance();
      auto operand = parse_factor();
      if (!operand.has_value()) {
        return std::nullopt;
      }
      return ast::Node::unary("-", std::move(*operand));
    }
    if (peek().kind == TokenKind::Integer) {
      return ast::Node::literal(advance().text);
    }
    if (peek().kind == TokenKind::Decimal) {
      auto canonical = canonicalize_decimal_literal(advance().text);
      if (!canonical.has_value()) {
        return std::nullopt;
      }
      return ast::Node::decimal_literal(std::move(canonical->coefficient_digits),
                                        canonical->exponent);
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
      if ((name == "quote" || name == "quasiquote") && peek_at(1).kind == TokenKind::LParen) {
        advance();
        advance();
        auto inner = parse_expr();
        if (!inner.has_value() || peek().kind != TokenKind::RParen) return std::nullopt;
        advance();
        // R24 only needs the R22 self-evaluating numeric-literal surface.
        // Other quoted forms remain unsupported rather than acquiring
        // incomplete quote semantics.
        if (inner->kind != ast::Kind::Literal && inner->kind != ast::Kind::DecimalLiteral) {
          return std::nullopt;
        }
        return ast::Node::quote(std::move(*inner), name == "quasiquote");
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
