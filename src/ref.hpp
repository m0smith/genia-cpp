#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include "value.hpp"

namespace genia::value {

class Ref {
 public:
  Ref() = default;
  explicit Ref(Value initial) : value_(std::move(initial)) {}

  Value get() const {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return value_.has_value(); });
    return *value_;
  }

  Value set(Value replacement) {
    Value result = replacement;
    {
      std::lock_guard lock(mutex_);
      value_ = std::move(replacement);
    }
    condition_.notify_all();
    return result;
  }

  bool is_set() const {
    std::lock_guard lock(mutex_);
    return value_.has_value();
  }

  Value update(const std::function<Value(const Value&)>& updater) {
    std::unique_lock lock(mutex_);
    condition_.wait(lock, [this] { return value_.has_value(); });
    Value replacement = updater(*value_);
    value_ = replacement;
    lock.unlock();
    condition_.notify_all();
    return replacement;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable condition_;
  std::optional<Value> value_;
};

}  // namespace genia::value
