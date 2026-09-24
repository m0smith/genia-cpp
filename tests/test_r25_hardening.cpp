#include <atomic>
#include <vector>

#include "../src/cell.hpp"
#include "../src/engine.hpp"
#include "../src/process.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::value::Cell;
using genia::value::Process;
using genia::value::Value;

namespace {
Value hardening_integer(std::uint64_t value) {
  return Value::make_integer(genia::bignum::Integer::from_u64(value));
}
}  // namespace

TEST_CASE("R25 Process never invokes one handler concurrently") {
  std::atomic<int> in_flight{0};
  std::atomic<int> maximum{0};
  auto process = Process::create([&](const Value& message) {
    const int current = ++in_flight;
    maximum.store(std::max(maximum.load(), current));
    --in_flight;
    return std::optional<Value>(message);
  });
  for (std::uint64_t i = 0; i < 1000; ++i) REQUIRE(process->send(hardening_integer(i)));
  Process::await_all_idle();
  CHECK(maximum.load() == 1);
}

TEST_CASE("R25 repeated Cell and Process create/destroy joins every worker") {
  for (std::uint64_t i = 0; i < 200; ++i) {
    auto cell = Cell::create(hardening_integer(i));
    REQUIRE(cell->send([](const Value& state) { return std::optional<Value>(state); }));
    cell->await_idle();
    auto process =
        Process::create([](const Value& message) { return std::optional<Value>(message); });
    REQUIRE(process->send(hardening_integer(i)));
    process->await_idle();
  }
}

TEST_CASE("R25 private idle hook is unavailable without its fixture") {
  CHECK_FALSE(genia::engine::try_run("_r25_await_idle()").has_value());
}

TEST_CASE("R25 post-failure Process send is a normalized runtime error") {
  auto result = genia::engine::try_run(
      "p = spawn((_) -> 1 / 0)\n"
      "send(p, 1)\n"
      "_r25_await_idle()\n"
      "send(p, 2)",
      true);
  REQUIRE(result.has_value());
  CHECK(result->stdout_text.empty());
  CHECK(result->stderr_text == "Error: send: process is failed\n");
  CHECK(result->exit_code == 1);
}
