// Native scheduling tests for private R25 causal accounting, not new semantics.
#include <future>
#include <stdexcept>

#include "../src/process.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::value::Cell;
using genia::value::Process;
using genia::value::stage_cell_send;
using genia::value::Value;

namespace {
Value number(unsigned n) { return Value::make_integer(genia::bignum::Integer::from_u64(n)); }
}  // namespace

// Read exactly the predicate used by await_idle while dispatch is causally paused.
// This avoids timing assertions about whether a separate waiter was scheduled.
namespace genia::value {
struct CellQuiescenceProbe {
  // Inspect the exact shared barrier predicate, without scheduling a waiter.
  static bool globally_settled() {
    std::lock_guard lock(R25Quiescence::mutex_);
    return R25Quiescence::pending_ == 0;
  }

  // Wait for destruction to request termination while its worker is gated.
  // The test retains the raw address only until it releases that worker.
  static void await_termination(Cell& cell) {
    std::unique_lock lock(cell.mutex_);
    cell.condition_.wait(lock, [&] { return cell.terminate_; });
  }

  static bool settled(const Cell& cell) {
    std::lock_guard lock(cell.mutex_);
    return cell.accepted_ == cell.completed_;
  }
};
}  // namespace genia::value

namespace genia::value {
// Observe precisely the local wait predicate while dispatch is held by a promise.
struct ProcessQuiescenceProbe {
  static bool settled(const Process& process) {
    std::lock_guard lock(process.mutex_);
    return process.accepted_ == process.completed_;
  }
};
}  // namespace genia::value

TEST_CASE("R25 Process stays outstanding through every committed action", "[quiescence]") {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(0));
  auto process = Process::create([&](const Value&) {
    stage_cell_send([&] { target->send([](const Value&) { return number(1); }); });
    stage_cell_send([&, gate] {
      entered.set_value();
      gate.wait();
      target->send([](const Value&) { return number(2); });
    });
    return std::optional<Value>(number(0));
  });
  REQUIRE(process->send(number(0)));
  entered.get_future().wait();
  CHECK_FALSE(genia::value::ProcessQuiescenceProbe::settled(*process));
  CHECK_FALSE(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  Process::await_all_idle();
  CHECK(target->get()->integer.to_decimal_string() == "2");
}

TEST_CASE("R25 causal barrier follows alternating new Cell and Process targets", "[quiescence]") {
  // Each callback admits the next target before retiring itself. Targets
  // may be created after the wait starts; a registry snapshot cannot suffice.
  std::vector<std::shared_ptr<Cell>> cells;
  std::vector<std::shared_ptr<Process>> processes;
  auto result = std::make_shared<genia::value::Ref>(number(0));
  std::function<void(unsigned)> launch;
  launch = [&](unsigned remaining) {
    if (remaining == 0) {
      result->set(number(42));
      return;
    }
    if (remaining % 2 == 0) {
      auto cell = Cell::create(number(0));
      cells.push_back(cell);
      cell->send([&, remaining](const Value&) {
        stage_cell_send([&, remaining] { launch(remaining - 1); });
        return number(1);
      });
    } else {
      auto process = Process::create([&, remaining](const Value&) {
        stage_cell_send([&, remaining] { launch(remaining - 1); });
        return std::optional<Value>(number(1));
      });
      processes.push_back(process);
      process->send(number(0));
    }
  };
  launch(12);
  Cell::await_all_idle();
  CHECK(result->get().integer.to_decimal_string() == "42");
  CHECK(cells.size() == 6);
  CHECK(processes.size() == 6);
}

TEST_CASE("R25 stale failed and stopped generations retire exactly their work", "[quiescence]") {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto parent = Cell::create(number(0));
  REQUIRE(parent->send([&, gate](const Value&) -> std::optional<Value> {
    stage_cell_send([&] { target->send([](const Value&) { return number(999); }); });
    entered.set_value();
    gate.wait();
    throw std::runtime_error("private stale error");
  }));
  entered.get_future().wait();
  REQUIRE(parent->send([](const Value&) { return number(999); }));
  parent->restart(number(20));
  REQUIRE(parent->send([](const Value&) { return number(21); }));
  parent->stop();
  release.set_value();
  Cell::await_all_idle();
  CHECK(parent->get()->integer.to_decimal_string() == "21");
  CHECK(parent->status() == Cell::Status::Stopped);
  CHECK_FALSE(parent->error().has_value());
  CHECK(target->get()->integer.to_decimal_string() == "10");
}

TEST_CASE("R25 discarded Process transaction cannot admit descendants", "[quiescence]") {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto process = Process::create([&, gate](const Value&) -> std::optional<Value> {
    stage_cell_send([&] { target->send([](const Value&) { return number(999); }); });
    entered.set_value();
    gate.wait();
    throw std::runtime_error("private handler error");
  });
  REQUIRE(process->send(number(0)));
  entered.get_future().wait();
  REQUIRE(process->send(number(1)));
  release.set_value();
  Cell::await_all_idle();
  CHECK(process->failed());
  CHECK(process->error() == "process handler failed");
  CHECK_FALSE(process->send(number(2)));
  CHECK(target->get()->integer.to_decimal_string() == "10");
}

TEST_CASE("R25 committed self-send runs without holding the Cell lock", "[quiescence]") {
  auto cell = Cell::create(number(0));
  REQUIRE(cell->send([&](const Value&) {
    stage_cell_send([&] { cell->send([](const Value&) { return number(2); }); });
    return number(1);
  }));
  Cell::await_all_idle();
  CHECK(cell->get()->integer.to_decimal_string() == "2");
}

TEST_CASE("R25 paused committed action remains outstanding", "[quiescence]") {
  std::promise<void> entered;
  std::promise<void> release;
  auto gate = release.get_future().share();
  auto target = Cell::create(number(10));
  auto parent = Cell::create(number(0));
  REQUIRE(parent->send([&](const Value&) {
    genia::value::stage_cell_send([&, gate] {
      entered.set_value();
      gate.wait();
      target->send([](const Value&) { return std::optional<Value>(number(15)); });
    });
    return std::optional<Value>(number(1));
  }));
  entered.get_future().wait();
  CHECK(parent->get()->integer.to_decimal_string() == "1");
  CHECK(target->get()->integer.to_decimal_string() == "10");
  CHECK_FALSE(genia::value::CellQuiescenceProbe::settled(*parent));
  CHECK_FALSE(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  Cell::await_all_idle();
  CHECK(target->get()->integer.to_decimal_string() == "15");
}

TEST_CASE("R25 destruction retires queued work after joining the active worker", "[quiescence]") {
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  auto cell = Cell::create(number(0));
  REQUIRE(cell->send([&, gate](const Value&) {
    entered.set_value();
    gate.wait();
    return number(1);
  }));
  entered.get_future().wait();
  bool queued_ran = false;
  REQUIRE(cell->send([&](const Value&) {
    queued_ran = true;
    return number(99);
  }));
  auto* address = cell.get();
  std::thread destroyer([owner = std::move(cell)]() mutable { owner.reset(); });
  genia::value::CellQuiescenceProbe::await_termination(*address);
  CHECK_FALSE(genia::value::CellQuiescenceProbe::globally_settled());
  release.set_value();
  destroyer.join();
  Cell::await_all_idle();
  CHECK_FALSE(queued_ran);
  CHECK(genia::value::CellQuiescenceProbe::globally_settled());
}
