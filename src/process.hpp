// Bounded C++ R25 local Process realization: a shared-identity handle owns a
// FIFO mailbox and one handler worker. Native failure/admission observations
// are bridged by the evaluator; thread/registry choices are host-local, not
// portable Actor, scheduler, supervision or distributed-message semantics.
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

// Own a handler, queued Values and a worker until the last shared handle dies.
// Handler failure permanently closes admission and discards queued messages.
// Avoid ownership cycles through captures/messages; destruction joins the worker.
class Process : public std::enable_shared_from_this<Process> {
 public:
  // A present result means handled (its Value is ignored); nullopt or exception
  // fails the process. Value-level Outcomes are still present Values, not exceptions.
  using Handler = std::function<std::optional<Value>(const Value&)>;

  // Move the handler into a new process owning its worker; the caller owns the returned handle.
  // Allocation/thread-start failures propagate; no message is invoked at creation.
  static std::shared_ptr<Process> create(Handler handler) {
    auto process = std::shared_ptr<Process>(new Process(std::move(handler)));
    return process;
  }

  // Request termination and join: queued messages need not be drained and an
  // active handler is not interrupted. Join may block forever if the handler does.
  // The last owner must release off-worker to avoid a self-join.
  ~Process() {
    {
      // Protect termination state before notifying the owned worker.
      std::lock_guard lock(mutex_);
      terminate_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    R25Quiescence::complete(messages_.size());
  }

  // Move a message into FIFO admission order, or return false after failure.
  // True means queued, not handled; there is no capacity/backpressure bound.
  // Queue allocation failures propagate and no retry/restart is supplied.
  bool send(Value message) {
    // Serialize admission with worker failure and mailbox accounting.
    std::lock_guard lock(mutex_);
    if (failed_) return false;
    messages_.push_back(std::move(message));
    ++accepted_;
    R25Quiescence::accept();
    condition_.notify_all();
    return true;
  }

  // Snapshot open admission (!failed), not std::thread liveness: a failed worker
  // remains waiting in this realization until destruction requests termination.
  bool alive() const {
    // Observe admission state consistently with worker failure.
    std::lock_guard lock(mutex_);
    return !failed_;
  }
  // Snapshot permanent handler-failure state without waiting for queued messages.
  bool failed() const {
    // Observe failure state consistently with worker completion.
    std::lock_guard lock(mutex_);
    return failed_;
  }
  // Copy cached "process handler failed" text, or nullopt before handler failure.
  // Raw exception text is not retained; this does not await message completion.
  std::optional<std::string> error() const {
    // Copy cached error consistently with worker failure recording.
    std::lock_guard lock(mutex_);
    return error_;
  }
  // Private evidence wait: block without timeout until accepted messages are
  // handled/discarded, including committed staged dispatch. Descendant work is
  // joined by the shared causal barrier, not this entity-local wait.
  void await_idle() const {
    // Release the mutex during idle waiting and recheck the counter predicate.
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return accepted_ == completed_; });
  }

  // Private shared causal barrier, including Cell targets and new entities.
  // External root admissions must precede this call; callbacks can block forever.
  static void await_all_idle() { R25Quiescence::await_idle(); }

 private:
  friend struct ProcessQuiescenceProbe;
  // Own the handler and start the worker; create establishes shared ownership
  // before use. Worker invocation begins only after a message arrives.
  explicit Process(Handler handler) : handler_(std::move(handler)), worker_([this] { run(); }) {}

  // Consume FIFO messages one at a time outside the process lock. Stage nested
  // sends thread-locally; nullopt/handler exception clears queued work, records
  // stable failure text and discards that invocation's staged actions. Success
  // dispatches staged actions outside the lock before completing the message.
  // Staged-action exceptions are uncaught here; handlers are not cancelled and
  // failure does not roll back other effects already performed by the handler.
  void run() {
    for (;;) {
      Value message;
      {
        // Release the mutex while waiting, then own dequeue/termination checks.
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
        // Commit completion or failure/queue discard atomically with admission.
        std::lock_guard lock(mutex_);
        if (!result.has_value()) {
          failed_ = true;
          error_ = "process handler failed";
          completed_ += 1 + messages_.size();
          R25Quiescence::complete(1 + messages_.size());
          messages_.clear();
        }
        idle_.notify_all();
      }
      if (result.has_value()) {
        for (auto& action : staged) action();
        // Retire the parent only after all committed child admissions finish.
        std::lock_guard lock(mutex_);
        ++completed_;
        R25Quiescence::complete();
        idle_.notify_all();
      }
    }
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
