// Lexical environments for the E24-4 vertical slice.
//
// E24-2/E24-3 used a single flat `std::unordered_map` for the whole
// program (no nested scopes were needed: no closures, no function
// calls introducing local bindings). E24-4 introduces lambdas and
// named-function calls, each of which evaluates its body in a fresh
// child scope (its call-time argument bindings) chained to the
// environment captured when the closure was created -- exactly
// genia-2026's own `Env(parent)` model (src/genia/environment.py).
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "value.hpp"

namespace genia::evaluator {

class Environment : public std::enable_shared_from_this<Environment> {
 public:
  explicit Environment(std::shared_ptr<Environment> parent = nullptr)
      : parent_(std::move(parent)) {}

  std::optional<value::Value> lookup(const std::string& name) const {
    auto it = bindings_.find(name);
    if (it != bindings_.end()) {
      return it->second;
    }
    if (parent_ != nullptr) {
      return parent_->lookup(name);
    }
    return std::nullopt;
  }

  void define(const std::string& name, value::Value v) { bindings_[name] = std::move(v); }

 private:
  std::shared_ptr<Environment> parent_;
  std::unordered_map<std::string, value::Value> bindings_;
};

using EnvPtr = std::shared_ptr<Environment>;

}  // namespace genia::evaluator
