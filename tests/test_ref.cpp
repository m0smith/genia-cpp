#include <atomic>
#include <thread>

#include "../src/engine.hpp"
#include "../src/ref.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::engine::try_run;
using genia::value::Ref;
using genia::value::Value;

TEST_CASE("R25 Ref blocks until a producer sets the exact value") {
  auto ref = std::make_shared<Ref>();
  std::atomic<bool> entered{false};
  Value observed;
  std::thread consumer([&] {
    entered.store(true);
    observed = ref->get();
  });
  while (!entered.load()) {
    std::this_thread::yield();
  }
  CHECK_FALSE(ref->is_set());
  ref->set(Value::make_integer(genia::bignum::Integer::from_i64(42)));
  consumer.join();
  REQUIRE(observed.kind == genia::value::Kind::Integer);
  CHECK(observed.integer.to_string() == "42");
}

TEST_CASE("R25 Ref update is serialized and returns every exact replacement") {
  auto ref = std::make_shared<Ref>(Value::make_integer(genia::bignum::Integer::from_i64(0)));
  constexpr int kPerProducer = 200;
  auto increment = [&] {
    for (int i = 0; i < kPerProducer; ++i) {
      ref->update([](const Value& current) {
        return Value::make_integer(current.integer.add(genia::bignum::Integer::from_i64(1)));
      });
    }
  };
  std::thread first(increment);
  std::thread second(increment);
  first.join();
  second.join();
  CHECK(ref->get().integer.to_string() == "400");
}

TEST_CASE("R25 shared Ref basic observation runs through the C++ engine") {
  auto result = try_run(
      "r = ref(10)\n"
      "set_result = ref_set(r, 20)\n"
      "update_result = ref_update(r, (x) -> x + 2)\n"
      "[ref_is_set(r), set_result, update_result, ref_get(r)]");
  REQUIRE(result.has_value());
  CHECK(result->stdout_text == "[true, 20, 22, 22]\n");
  CHECK(result->stderr_text.empty());
  CHECK(result->exit_code == 0);
}
