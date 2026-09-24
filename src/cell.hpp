#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ref.hpp"

namespace genia::value {

inline thread_local std::vector<std::function<void()>>* g_cell_staged_sends = nullptr;
inline thread_local bool g_r25_fixture_enabled = false;

inline bool stage_cell_send(std::function<void()> action) {
  if (g_cell_staged_sends == nullptr) return false;
  g_cell_staged_sends->push_back(std::move(action));
  return true;
}

class Cell : public std::enable_shared_from_this<Cell> {
 public:
  enum class Status { Ready, Stopped, Failed };
  using Update = std::function<std::optional<Value>(const Value&)>;

  static std::shared_ptr<Cell> create(Value initial) {
    return create_with_state(std::make_shared<Ref>(std::move(initial)));
  }

  static std::shared_ptr<Cell> create_with_state(std::shared_ptr<Ref> state) {
    auto cell = std::shared_ptr<Cell>(new Cell(std::move(state)));
    {
      std::lock_guard lock(registry_mutex());
      registry().push_back(cell);
    }
    return cell;
  }

  ~Cell() {
    {
      std::lock_guard lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  bool send(Update update) {
    std::lock_guard lock(mutex_);
    if (status_ != Status::Ready) return false;
    queue_.push_back(Work{generation_, std::move(update)});
    ++accepted_;
    condition_.notify_all();
    return true;
  }

  std::optional<Value> get() const {
    std::lock_guard lock(mutex_);
    if (status_ == Status::Failed) return std::nullopt;
    return state_->get();
  }

  std::shared_ptr<Ref> backing_state() const { return state_; }
  Status status() const {
    std::lock_guard lock(mutex_);
    return status_;
  }
  std::optional<std::string> error() const {
    std::lock_guard lock(mutex_);
    return error_;
  }

  void stop() {
    std::lock_guard lock(mutex_);
    if (status_ == Status::Ready) status_ = Status::Stopped;
    condition_.notify_all();
  }

  std::shared_ptr<Cell> restart(Value new_state) {
    std::lock_guard lock(mutex_);
    ++generation_;
    completed_ += queue_.size();
    queue_.clear();
    state_->set(std::move(new_state));
    status_ = Status::Ready;
    error_.reset();
    condition_.notify_all();
    return shared_from_this();
  }

  void await_idle() const {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return accepted_ == completed_; });
  }

  static void await_all_idle() {
    for (;;) {
      std::vector<std::shared_ptr<Cell>> snapshot;
      {
        std::lock_guard lock(registry_mutex());
        auto& entries = registry();
        for (auto it = entries.begin(); it != entries.end();) {
          if (auto cell = it->lock()) {
            snapshot.push_back(std::move(cell));
            ++it;
          } else {
            it = entries.erase(it);
          }
        }
      }
      for (const auto& cell : snapshot) cell->await_idle();
      bool settled = true;
      for (const auto& cell : snapshot) {
        std::lock_guard lock(cell->mutex_);
        settled = settled && cell->accepted_ == cell->completed_;
      }
      if (settled) return;
    }
  }

 private:
  struct Work {
    std::uint64_t generation;
    Update update;
  };

  explicit Cell(std::shared_ptr<Ref> state)
      : state_(std::move(state)), worker_([this] { run(); }) {}

  void run() {
    for (;;) {
      Work work;
      {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return terminate_ || !queue_.empty(); });
        if (terminate_) return;
        work = std::move(queue_.front());
        queue_.pop_front();
      }
      std::vector<std::function<void()>> staged;
      g_cell_staged_sends = &staged;
      std::optional<Value> replacement;
      try {
        replacement = work.update(state_->get());
      } catch (...) {
        replacement.reset();
      }
      g_cell_staged_sends = nullptr;
      bool commit_staged = false;
      {
        std::lock_guard lock(mutex_);
        if (work.generation != generation_) {
          ++completed_;
        } else if (!replacement.has_value()) {
          status_ = Status::Failed;
          error_ = "cell update failed";
          completed_ += 1 + queue_.size();
          queue_.clear();
        } else {
          state_->set(std::move(*replacement));
          ++completed_;
          commit_staged = true;
        }
        idle_.notify_all();
      }
      if (commit_staged)
        for (auto& action : staged) action();
    }
  }

  static std::vector<std::weak_ptr<Cell>>& registry() {
    static std::vector<std::weak_ptr<Cell>> cells;
    return cells;
  }
  static std::mutex& registry_mutex() {
    static std::mutex mutex;
    return mutex;
  }

  std::shared_ptr<Ref> state_;
  mutable std::mutex mutex_;
  mutable std::condition_variable idle_;
  std::condition_variable condition_;
  std::deque<Work> queue_;
  Status status_ = Status::Ready;
  std::optional<std::string> error_;
  std::uint64_t generation_ = 0;
  std::size_t accepted_ = 0;
  std::size_t completed_ = 0;
  bool terminate_ = false;
  std::thread worker_;
};

}  // namespace genia::value
