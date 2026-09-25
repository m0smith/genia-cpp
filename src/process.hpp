#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cell.hpp"

namespace genia::value {

class Process : public std::enable_shared_from_this<Process> {
 public:
  using Handler = std::function<std::optional<Value>(const Value&)>;

  static std::shared_ptr<Process> create(Handler handler) {
    auto process = std::shared_ptr<Process>(new Process(std::move(handler)));
    std::lock_guard lock(registry_mutex());
    registry().push_back(process);
    return process;
  }

  ~Process() {
    {
      std::lock_guard lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  bool send(Value message) {
    std::lock_guard lock(mutex_);
    if (failed_) return false;
    messages_.push_back(std::move(message));
    ++accepted_;
    condition_.notify_all();
    return true;
  }

  bool alive() const {
    std::lock_guard lock(mutex_);
    return !failed_;
  }
  bool failed() const {
    std::lock_guard lock(mutex_);
    return failed_;
  }
  std::optional<std::string> error() const {
    std::lock_guard lock(mutex_);
    return error_;
  }
  void await_idle() const {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return accepted_ == completed_; });
  }

  static void await_all_idle() {
    std::vector<std::shared_ptr<Process>> snapshot;
    {
      std::lock_guard lock(registry_mutex());
      auto& entries = registry();
      for (auto it = entries.begin(); it != entries.end();) {
        if (auto process = it->lock()) {
          snapshot.push_back(std::move(process));
          ++it;
        } else {
          it = entries.erase(it);
        }
      }
    }
    for (const auto& process : snapshot) process->await_idle();
  }

 private:
  explicit Process(Handler handler) : handler_(std::move(handler)), worker_([this] { run(); }) {}

  void run() {
    for (;;) {
      Value message;
      {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return terminate_ || !messages_.empty(); });
        if (terminate_) return;
        message = std::move(messages_.front());
        messages_.pop_front();
      }
      std::vector<std::function<void()>> staged;
      g_cell_staged_sends = &staged;
      std::optional<Value> result;
      try {
        result = handler_(message);
      } catch (...) {
        result.reset();
      }
      g_cell_staged_sends = nullptr;
      {
        std::lock_guard lock(mutex_);
        if (!result.has_value()) {
          failed_ = true;
          error_ = "process handler failed";
          completed_ += 1 + messages_.size();
          messages_.clear();
        } else {
          ++completed_;
        }
        idle_.notify_all();
      }
      if (result.has_value())
        for (auto& action : staged) action();
    }
  }

  static std::vector<std::weak_ptr<Process>>& registry() {
    static std::vector<std::weak_ptr<Process>> processes;
    return processes;
  }
  static std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
  }

  Handler handler_;
  mutable std::mutex mutex_;
  mutable std::condition_variable idle_;
  std::condition_variable condition_;
  std::deque<Value> messages_;
  bool failed_ = false;
  bool terminate_ = false;
  std::optional<std::string> error_;
  std::size_t accepted_ = 0;
  std::size_t completed_ = 0;
  std::thread worker_;
};

}  // namespace genia::value
