// R20 contract section 3.3 (genia-2026 docs/design/r20-open-functions-contract.md):
// header-name capture check for grouped open-function clauses.
//
// In `open f(h1, ..., hn) = (p1, ...) -> r1 | ...` the plain-identifier header
// names are not bindings; each arm is an independent clause. An arm result
// that refers freely to a header name its own pattern does not bind would
// silently resolve to an outer or global binding of that name, so the
// reference host rejects the program (`open-function-header-capture`).
//
// This host has no parse-diagnostic channel (see parser.hpp: an open
// redeclaration is likewise not diagnosed), so the parser uses this check to
// stop accepting such a program, which the adapter reports as unsupported
// rather than run with a guessed meaning.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "ast.hpp"
#include "pattern.hpp"

namespace genia::header_capture {

// Adds every name that `pattern` binds (identifier binds and named `..rest`
// remainders, at any depth) to `names`. Wildcards, literals, and an unnamed
// rest bind nothing. Pure; never fails.
inline void collect_bound_names(const pattern::Pattern& pattern, std::set<std::string>& names) {
  switch (pattern.kind) {
    case pattern::Kind::Bind:
    case pattern::Kind::Rest:
      if (!pattern.name.empty()) {
        names.insert(pattern.name);
      }
      break;
    case pattern::Kind::List:
    case pattern::Kind::Tuple:
    case pattern::Kind::Err:
    case pattern::Kind::Some:
      for (const auto& item : pattern.items) {
        collect_bound_names(item, names);
      }
      break;
    case pattern::Kind::Map:
      for (const auto& entry : pattern.map_items) {
        collect_bound_names(entry.value, names);
      }
      break;
    case pattern::Kind::Wildcard:
    case pattern::Kind::Literal:
      break;
  }
}

// True when `node` refers to a name in `watched` that no binder inside it
// introduces. `bound` holds the names already introduced by enclosing binders
// within the arm: lambda parameters and earlier assignments in the same block.
// A call's callee name is a reference; quoted data is not. Pure; never fails.
inline bool has_free_reference(const ast::Node& node, const std::set<std::string>& watched,
                               const std::set<std::string>& bound) {
  auto refers = [&](const std::string& name) {
    return watched.count(name) > 0 && bound.count(name) == 0;
  };
  auto any_of_items = [&](const std::vector<ast::Node>& items) {
    for (const auto& item : items) {
      if (has_free_reference(item, watched, bound)) return true;
    }
    return false;
  };
  switch (node.kind) {
    case ast::Kind::Var:
      return refers(node.name);
    case ast::Kind::Call:
      return refers(node.name) || any_of_items(node.items);
    case ast::Kind::Binary:
      return (node.left && has_free_reference(*node.left, watched, bound)) ||
             (node.right && has_free_reference(*node.right, watched, bound));
    case ast::Kind::Unary:
    case ast::Kind::Spread:
    case ast::Kind::Assign:
      return node.left && has_free_reference(*node.left, watched, bound);
    case ast::Kind::List:
      return any_of_items(node.items);
    case ast::Kind::Map:
      for (const auto& entry : node.map_entries) {
        if (has_free_reference(entry.value, watched, bound)) return true;
      }
      return false;
    case ast::Kind::Block: {
      std::set<std::string> scope = bound;
      for (const auto& item : node.items) {
        if (has_free_reference(item, watched, scope)) return true;
        if (item.kind == ast::Kind::Assign) {
          scope.insert(item.name);
        }
      }
      return false;
    }
    case ast::Kind::Lambda: {
      std::set<std::string> scope = bound;
      for (const auto& param : node.params) {
        collect_bound_names(param, scope);
      }
      return node.left && has_free_reference(*node.left, watched, scope);
    }
    case ast::Kind::Literal:
    case ast::Kind::StringLiteral:
    case ast::Kind::BoolLiteral:
    case ast::Kind::DecimalLiteral:
    case ast::Kind::Quote:
    case ast::Kind::QuasiQuote:
    case ast::Kind::FuncDef:
    case ast::Kind::OpenFuncDef:
      return false;
  }
  return false;
}

// True when a grouped clause must be rejected: some header name of
// `header_patterns` (plain identifier binds) is referenced freely by `result`
// although `arm_pattern` does not bind it.
inline bool arm_captures_header_name(const std::vector<pattern::Pattern>& header_patterns,
                                     const pattern::Pattern& arm_pattern, const ast::Node& result) {
  std::set<std::string> arm_bound;
  collect_bound_names(arm_pattern, arm_bound);
  std::set<std::string> watched;
  for (const auto& header : header_patterns) {
    if (header.kind == pattern::Kind::Bind && arm_bound.count(header.name) == 0) {
      watched.insert(header.name);
    }
  }
  if (watched.empty()) {
    return false;
  }
  return has_free_reference(result, watched, {});
}

}  // namespace genia::header_capture
