// Unit tests for the E24-2 in-house bignum kernel (src/bignum.hpp).
#include "../src/bignum.hpp"
#include "../third_party/catch2/catch.hpp"

using genia::bignum::Integer;

TEST_CASE("from_unsigned_decimal parses simple digit strings") {
  auto value = Integer::from_unsigned_decimal("42");
  REQUIRE(value.has_value());
  CHECK(value->to_decimal_string() == "42");
}

TEST_CASE("from_unsigned_decimal rejects non-digit input") {
  CHECK_FALSE(Integer::from_unsigned_decimal("").has_value());
  CHECK_FALSE(Integer::from_unsigned_decimal("12a").has_value());
  CHECK_FALSE(Integer::from_unsigned_decimal("-5").has_value());
  CHECK_FALSE(Integer::from_unsigned_decimal(" 5").has_value());
}

TEST_CASE("zero round-trips including leading zeros") {
  auto value = Integer::from_unsigned_decimal("0000");
  REQUIRE(value.has_value());
  CHECK(value->to_decimal_string() == "0");
  CHECK(value->is_zero());
}

TEST_CASE("addition matches arithmetic-basic.yaml: 40 + 2 == 42") {
  auto lhs = Integer::from_unsigned_decimal("40");
  auto rhs = Integer::from_unsigned_decimal("2");
  REQUIRE(lhs.has_value());
  REQUIRE(rhs.has_value());
  CHECK(lhs->add(*rhs).to_decimal_string() == "42");
}

TEST_CASE("multiplication of 34-digit numbers matches the pinned no-overflow case exactly") {
  auto lhs = Integer::from_unsigned_decimal("99999999999999999999999999999999");
  auto rhs = Integer::from_unsigned_decimal("99999999999999999999999999999999");
  REQUIRE(lhs.has_value());
  REQUIRE(rhs.has_value());
  CHECK(lhs->mul(*rhs).to_decimal_string() ==
        "9999999999999999999999999999999800000000000000000000000000000001");
}

TEST_CASE("subtraction handles sign correctly") {
  auto ten = Integer::from_unsigned_decimal("10");
  auto three = Integer::from_unsigned_decimal("3");
  REQUIRE(ten.has_value());
  REQUIRE(three.has_value());
  CHECK(ten->sub(*three).to_decimal_string() == "7");
  CHECK(three->sub(*ten).to_decimal_string() == "-7");
}

TEST_CASE("negate and comparisons") {
  auto five = Integer::from_unsigned_decimal("5");
  REQUIRE(five.has_value());
  Integer neg_five = five->negate();
  CHECK(neg_five.to_decimal_string() == "-5");
  CHECK(Integer::compare(neg_five, *five) < 0);
  CHECK(Integer::compare(*five, *five) == 0);
  CHECK(Integer::compare(*five, neg_five) > 0);
}

TEST_CASE("exact_divide succeeds only for evenly-dividing quotients") {
  auto six = Integer::from_unsigned_decimal("6");
  auto three = Integer::from_unsigned_decimal("3");
  auto two = Integer::from_unsigned_decimal("2");
  auto zero = Integer::from_unsigned_decimal("0");
  REQUIRE(six.has_value());
  REQUIRE(three.has_value());
  REQUIRE(two.has_value());
  REQUIRE(zero.has_value());

  auto quotient = six->exact_divide(*three);
  REQUIRE(quotient.has_value());
  CHECK(quotient->to_decimal_string() == "2");

  // 6 / 2 is not evenly-dividing by three's own quotient path but IS
  // evenly divisible by 2: 6/2 == 3.
  auto six_by_two = six->exact_divide(*two);
  REQUIRE(six_by_two.has_value());
  CHECK(six_by_two->to_decimal_string() == "3");

  // R22: 1/2 is not an Integer result (it is Rational 1/2), which this
  // slice does not implement -- must be std::nullopt, never a truncated
  // or rounded Integer.
  auto one = Integer::from_unsigned_decimal("1");
  REQUIRE(one.has_value());
  CHECK_FALSE(one->exact_divide(*two).has_value());

  // Division by exact zero is deterministic numeric misuse (R22), not
  // representable by this slice either.
  CHECK_FALSE(six->exact_divide(*zero).has_value());
}

TEST_CASE("mul by zero is zero") {
  auto value = Integer::from_unsigned_decimal("123456789123456789");
  auto zero = Integer::from_unsigned_decimal("0");
  REQUIRE(value.has_value());
  REQUIRE(zero.has_value());
  CHECK(value->mul(*zero).to_decimal_string() == "0");
  CHECK(zero->mul(*value).to_decimal_string() == "0");
}
