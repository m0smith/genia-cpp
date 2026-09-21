// In-house arbitrary-precision Integer kernel (sign + base-2^32 limb
// vector), per docs/design/r24/dependency-toolchain-policy.md and
// docs/design/r24/native-primitive-inventory.md in genia-2026: no
// third-party bignum library, since only an in-house kernel is
// guaranteed to match R17's exact, overflow-free arithmetic contract
// bit-for-bit rather than "close enough" via someone else's rounding or
// limit choices.
//
// This is Core-IR-adjacent runtime plumbing (native primitive), not
// portable Genia semantics: it backs the exact Integer runtime value,
// never exposed to Genia source directly.
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace genia::bignum {

// Sign + magnitude arbitrary-precision integer. Magnitude is stored as
// base-2^32 limbs, least-significant limb first. Zero is represented as
// an empty limb vector with positive_ == true (canonical zero has no
// sign).
class Integer {
 public:
  Integer() = default;

  static Integer from_u64(uint64_t value) {
    Integer result;
    if (value == 0) {
      return result;
    }
    result.limbs_.push_back(static_cast<uint32_t>(value & 0xFFFFFFFFu));
    const uint32_t high = static_cast<uint32_t>(value >> 32);
    if (high != 0) {
      result.limbs_.push_back(high);
    }
    return result;
  }

  // Parses an unsigned decimal digit string (no sign, no leading '+',
  // may have leading zeros). Returns std::nullopt if any character is
  // not an ASCII digit or the string is empty.
  static std::optional<Integer> from_unsigned_decimal(const std::string& digits) {
    if (digits.empty()) {
      return std::nullopt;
    }
    for (const char c : digits) {
      if (c < '0' || c > '9') {
        return std::nullopt;
      }
    }
    Integer result;
    // Base-10^9 chunks converted via repeated multiply-by-10^9 + add,
    // which keeps the algorithm simple and correct without needing a
    // base-conversion routine; performance is not a concern at this
    // slice's scale (bootstrap evidence uses <= ~34-digit literals).
    constexpr uint32_t kChunkBase = 1000000000u;
    size_t pos = 0;
    // First chunk may be shorter than 9 digits.
    size_t first_len = digits.size() % 9;
    if (first_len == 0) {
      first_len = 9;
    }
    uint32_t first_chunk = static_cast<uint32_t>(std::stoul(digits.substr(0, first_len)));
    result = Integer::from_u64(first_chunk);
    pos = first_len;
    while (pos < digits.size()) {
      uint32_t chunk = static_cast<uint32_t>(std::stoul(digits.substr(pos, 9)));
      result = result.mul_u32(kChunkBase).add_magnitude(Integer::from_u64(chunk));
      pos += 9;
    }
    result.normalize();
    return result;
  }

  bool is_zero() const { return limbs_.empty(); }
  bool is_positive_or_zero() const { return positive_; }

  Integer negate() const {
    Integer result = *this;
    if (!result.is_zero()) {
      result.positive_ = !result.positive_;
    }
    return result;
  }

  static int compare_magnitude(const Integer& a, const Integer& b) {
    if (a.limbs_.size() != b.limbs_.size()) {
      return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
    }
    for (size_t i = a.limbs_.size(); i-- > 0;) {
      if (a.limbs_[i] != b.limbs_[i]) {
        return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
      }
    }
    return 0;
  }

  static int compare(const Integer& a, const Integer& b) {
    if (a.is_zero() && b.is_zero()) {
      return 0;
    }
    if (a.positive_ != b.positive_) {
      if (a.is_zero() && !b.positive_) return 1;
      if (b.is_zero() && !a.positive_) return -1;
      return a.positive_ ? 1 : -1;
    }
    const int mag_cmp = compare_magnitude(a, b);
    return a.positive_ ? mag_cmp : -mag_cmp;
  }

  Integer add(const Integer& other) const {
    if (positive_ == other.positive_) {
      Integer result = add_magnitude(other);
      result.positive_ = positive_;
      result.normalize();
      return result;
    }
    // Opposite signs: subtract the smaller magnitude from the larger.
    if (compare_magnitude(*this, other) >= 0) {
      Integer result = sub_magnitude(*this, other);
      result.positive_ = positive_;
      result.normalize();
      return result;
    }
    Integer result = sub_magnitude(other, *this);
    result.positive_ = other.positive_;
    result.normalize();
    return result;
  }

  Integer sub(const Integer& other) const { return add(other.negate()); }

  Integer mul(const Integer& other) const {
    if (is_zero() || other.is_zero()) {
      return Integer();
    }
    std::vector<uint32_t> result_limbs(limbs_.size() + other.limbs_.size(), 0);
    for (size_t i = 0; i < limbs_.size(); ++i) {
      uint64_t carry = 0;
      const uint64_t a = limbs_[i];
      for (size_t j = 0; j < other.limbs_.size() || carry != 0; ++j) {
        const uint64_t b = (j < other.limbs_.size()) ? other.limbs_[j] : 0;
        const uint64_t sum = result_limbs[i + j] + a * b + carry;
        result_limbs[i + j] = static_cast<uint32_t>(sum & 0xFFFFFFFFu);
        carry = sum >> 32;
      }
    }
    Integer result;
    result.limbs_ = std::move(result_limbs);
    result.positive_ = (positive_ == other.positive_);
    result.normalize();
    return result;
  }

  // Exact division: returns the quotient only if `other` divides `this`
  // evenly (remainder zero); returns std::nullopt otherwise (including
  // division by zero). R22 defines non-evenly-dividing Integer/Integer
  // division as producing a Rational, which this slice does not
  // implement -- callers must treat std::nullopt as "not supported by
  // this slice", never as a language-level error.
  // Exact floor-remainder (`%`), per genia-2026's R22 contract
  // (src/genia/numeric_runtime.py's `exact_remainder`): `left - floor(left
  // / other) * other` -- Python-style, sign follows the divisor, distinct
  // from C++'s own truncating `%` (sign follows the dividend). Returns
  // std::nullopt for a zero divisor, matching exact_divide's convention.
  std::optional<Integer> floor_remainder(const Integer& other) const {
    if (other.is_zero()) {
      return std::nullopt;
    }
    if (is_zero()) {
      return Integer();
    }
    Integer magnitude_remainder;
    divmod_magnitude(*this, other, magnitude_remainder);
    // `magnitude_remainder` is |this| mod |other|, always >= 0. The
    // truncating remainder (this = trunc_quotient * other + trunc_rem)
    // takes its sign from `this` (or is zero).
    Integer truncating_remainder = magnitude_remainder;
    truncating_remainder.positive_ = positive_;
    truncating_remainder.normalize();
    if (truncating_remainder.is_zero()) {
      return truncating_remainder;
    }
    // Floor and truncating division agree unless the operands' signs
    // differ, in which case floor rounds one further away from zero:
    // floor_remainder = truncating_remainder + other.
    if (positive_ != other.positive_) {
      return truncating_remainder.add(other);
    }
    return truncating_remainder;
  }

  std::optional<Integer> exact_divide(const Integer& other) const {
    if (other.is_zero()) {
      return std::nullopt;
    }
    if (is_zero()) {
      return Integer();
    }
    Integer remainder;
    Integer quotient = divmod_magnitude(*this, other, remainder);
    if (!remainder.is_zero()) {
      return std::nullopt;
    }
    quotient.positive_ = (positive_ == other.positive_);
    quotient.normalize();
    return quotient;
  }

  // Canonical decimal string, sign included for negative values, no
  // leading zeros (matches R21's "canonical unsigned decimal text" for
  // the magnitude, with sign kept outside per the IrLiteral contract).
  std::string to_decimal_string() const {
    if (is_zero()) {
      return "0";
    }
    // Repeatedly divide the magnitude by 10^9, collecting base-10^9
    // chunks from least-significant to most-significant.
    std::vector<uint32_t> work = limbs_;
    std::vector<uint32_t> chunks;  // least-significant chunk first
    while (!(work.size() == 1 && work[0] == 0) && !work.empty()) {
      uint64_t remainder = 0;
      for (size_t i = work.size(); i-- > 0;) {
        const uint64_t cur = (remainder << 32) | work[i];
        work[i] = static_cast<uint32_t>(cur / 1000000000u);
        remainder = cur % 1000000000u;
      }
      while (work.size() > 1 && work.back() == 0) {
        work.pop_back();
      }
      chunks.push_back(static_cast<uint32_t>(remainder));
    }
    // Emit chunks most-significant first; only the most-significant
    // chunk is left unpadded, every other chunk is zero-padded to 9
    // digits.
    std::string digits;
    for (size_t i = chunks.size(); i-- > 0;) {
      std::string chunk = std::to_string(chunks[i]);
      if (i + 1 == chunks.size()) {
        digits += chunk;
      } else {
        digits += std::string(9 - chunk.size(), '0') + chunk;
      }
    }
    return positive_ ? digits : ("-" + digits);
  }

 private:
  std::vector<uint32_t> limbs_;  // little-endian, base 2^32
  bool positive_ = true;

  void normalize() {
    while (!limbs_.empty() && limbs_.back() == 0) {
      limbs_.pop_back();
    }
    if (limbs_.empty()) {
      positive_ = true;
    }
  }

  Integer mul_u32(uint32_t value) const {
    if (is_zero() || value == 0) {
      return Integer();
    }
    Integer result;
    result.limbs_.resize(limbs_.size() + 1, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < limbs_.size(); ++i) {
      const uint64_t product = static_cast<uint64_t>(limbs_[i]) * value + carry;
      result.limbs_[i] = static_cast<uint32_t>(product & 0xFFFFFFFFu);
      carry = product >> 32;
    }
    result.limbs_[limbs_.size()] = static_cast<uint32_t>(carry);
    result.positive_ = positive_;
    result.normalize();
    return result;
  }

  // Adds magnitudes only, ignoring sign.
  Integer add_magnitude(const Integer& other) const {
    Integer result;
    result.limbs_.resize(std::max(limbs_.size(), other.limbs_.size()) + 1, 0);
    uint64_t carry = 0;
    for (size_t i = 0; i < result.limbs_.size(); ++i) {
      const uint64_t a = (i < limbs_.size()) ? limbs_[i] : 0;
      const uint64_t b = (i < other.limbs_.size()) ? other.limbs_[i] : 0;
      const uint64_t sum = a + b + carry;
      result.limbs_[i] = static_cast<uint32_t>(sum & 0xFFFFFFFFu);
      carry = sum >> 32;
    }
    result.normalize();
    return result;
  }

  // Subtracts magnitudes only: requires |a| >= |b|.
  static Integer sub_magnitude(const Integer& a, const Integer& b) {
    Integer result;
    result.limbs_.resize(a.limbs_.size(), 0);
    int64_t borrow = 0;
    for (size_t i = 0; i < a.limbs_.size(); ++i) {
      const int64_t av = a.limbs_[i];
      const int64_t bv = (i < b.limbs_.size()) ? b.limbs_[i] : 0;
      int64_t diff = av - bv - borrow;
      if (diff < 0) {
        diff += (int64_t{1} << 32);
        borrow = 1;
      } else {
        borrow = 0;
      }
      result.limbs_[i] = static_cast<uint32_t>(diff);
    }
    result.normalize();
    return result;
  }

  // Schoolbook long division on magnitudes only; sets `remainder` (a
  // non-negative magnitude-only Integer) and returns the quotient
  // magnitude. Requires other is non-zero.
  static Integer divmod_magnitude(const Integer& dividend, const Integer& divisor,
                                  Integer& remainder) {
    remainder = Integer();
    if (compare_magnitude(dividend, divisor) < 0) {
      Integer zero;
      remainder = dividend;
      remainder.positive_ = true;
      return zero;
    }
    // Bit-by-bit long division: simple and correct, sufficient for this
    // slice's scale.
    std::vector<uint32_t> quotient_limbs(dividend.limbs_.size(), 0);
    Integer current;  // running remainder, magnitude only
    for (size_t limb_index = dividend.limbs_.size(); limb_index-- > 0;) {
      for (int bit = 31; bit >= 0; --bit) {
        // current = current * 2 + next bit of dividend
        current = current.shift_left_one_bit();
        const uint32_t bit_value = (dividend.limbs_[limb_index] >> bit) & 1u;
        if (bit_value != 0) {
          current = current.add_magnitude(Integer::from_u64(1));
        }
        Integer divisor_mag = divisor;
        divisor_mag.positive_ = true;
        if (compare_magnitude(current, divisor_mag) >= 0) {
          current = sub_magnitude(current, divisor_mag);
          quotient_limbs[limb_index] |= (uint32_t{1} << bit);
        }
      }
    }
    Integer quotient;
    quotient.limbs_ = std::move(quotient_limbs);
    quotient.positive_ = true;
    quotient.normalize();
    remainder = current;
    remainder.positive_ = true;
    remainder.normalize();
    return quotient;
  }

  Integer shift_left_one_bit() const {
    Integer result;
    result.limbs_.resize(limbs_.size() + 1, 0);
    uint32_t carry = 0;
    for (size_t i = 0; i < limbs_.size(); ++i) {
      const uint32_t value = limbs_[i];
      result.limbs_[i] = (value << 1) | carry;
      carry = value >> 31;
    }
    result.limbs_[limbs_.size()] = carry;
    result.positive_ = true;
    result.normalize();
    return result;
  }
};

}  // namespace genia::bignum
