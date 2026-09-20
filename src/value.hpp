// Runtime values for the E24-2 vertical slice.
//
// Only two kinds exist at this slice: exact Integer, and an opaque
// placeholder for a known-but-not-yet-implemented global binding (see
// global_env.hpp). Decimal/Rational/Float64/lists/maps/lambdas/Outcomes
// are all later slices' scope (E24-3/E24-4/E24-7) -- this is
// deliberately not a general Genia value representation yet.
#pragma once

#include <cstdint>

#include "bignum.hpp"

namespace genia::value {

enum class Kind : std::uint8_t { Integer, Opaque };

struct Value {
  Kind kind = Kind::Opaque;
  bignum::Integer integer;  // valid only when kind == Integer

  static Value make_integer(bignum::Integer value) {
    Value v;
    v.kind = Kind::Integer;
    v.integer = std::move(value);
    return v;
  }

  static Value make_opaque() {
    Value v;
    v.kind = Kind::Opaque;
    return v;
  }
};

}  // namespace genia::value
