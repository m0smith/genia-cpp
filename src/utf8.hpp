// In-house UTF-8 validation for the R26-2 `bytes_utf8` capability
// (genia-2026 issue #1024, docs/design/r26-cpp-data-bridge-contract.md).
//
// No ICU or other third-party Unicode library per the R24 pre-flight
// dependency/toolchain policy (genia-2026's
// docs/design/r24/dependency-toolchain-policy.md: "Unicode strategy:
// in-house UTF-8 decode/code-point iteration; no ICU").
//
// This validates a byte sequence is well-formed UTF-8 per RFC 3629: no
// overlong encodings, no encoded surrogate half (U+D800-U+DFFF), and no
// codepoint above U+10FFFF. It is used only to decide whether
// `utf8_decode`'s input is well-formed (the only case this slice's
// shared evidence, spec/eval/r19-unicode-utf8-encode-decode-roundtrip.yaml,
// exercises); malformed input is honestly `unsupported` by this slice
// (see native_functions.hpp), not a fabricated diagnostic, since no
// shared evidence pins the malformed-input error path yet
// (contract section 6: Genia source cannot construct arbitrary invalid
// UTF-8 bytes today).
#pragma once

#include <cstdint>
#include <string>

namespace genia::utf8 {

// Returns true iff `bytes` is a well-formed UTF-8 byte sequence.
inline bool is_well_formed(const std::string& bytes) {
  const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
  const std::size_t size = bytes.size();
  std::size_t i = 0;
  while (i < size) {
    const unsigned char lead = data[i];
    std::size_t extra_bytes = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t min_codepoint = 0;
    if (lead < 0x80) {
      ++i;
      continue;
    } else if ((lead & 0xE0) == 0xC0) {
      extra_bytes = 1;
      codepoint = lead & 0x1F;
      min_codepoint = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      extra_bytes = 2;
      codepoint = lead & 0x0F;
      min_codepoint = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      extra_bytes = 3;
      codepoint = lead & 0x07;
      min_codepoint = 0x10000;
    } else {
      // Invalid lead byte: a continuation byte (0x80-0xBF) or a
      // reserved/never-valid byte (0xF8-0xFF).
      return false;
    }
    if (i + extra_bytes >= size) {
      // Truncated multi-byte sequence at end of input.
      return false;
    }
    for (std::size_t k = 1; k <= extra_bytes; ++k) {
      const unsigned char continuation = data[i + k];
      if ((continuation & 0xC0) != 0x80) {
        return false;
      }
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    if (codepoint < min_codepoint) {
      // Overlong encoding.
      return false;
    }
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
      // Encoded surrogate half: never a valid UTF-8 scalar value.
      return false;
    }
    if (codepoint > 0x10FFFF) {
      return false;
    }
    i += 1 + extra_bytes;
  }
  return true;
}

// Decodes one codepoint starting at byte offset `i` of `data`/`size`,
// structurally permissive of an encoded surrogate half (needed so a
// JSON scanner can detect and reject one as `invalid_json_unicode`
// rather than treating it as generically malformed bytes). Returns the
// codepoint and its encoded byte length, or std::nullopt for a
// structurally malformed sequence (bad lead byte, truncated
// continuation, non-continuation byte, overlong encoding, or a
// codepoint above U+10FFFF).
struct DecodedCodepoint {
  std::uint32_t codepoint = 0;
  std::size_t length = 0;
};

inline std::optional<DecodedCodepoint> decode_one_permissive(const unsigned char* data,
                                                             std::size_t i, std::size_t size) {
  const unsigned char lead = data[i];
  std::size_t extra_bytes = 0;
  std::uint32_t codepoint = 0;
  std::uint32_t min_codepoint = 0;
  if (lead < 0x80) {
    return DecodedCodepoint{lead, 1};
  } else if ((lead & 0xE0) == 0xC0) {
    extra_bytes = 1;
    codepoint = lead & 0x1F;
    min_codepoint = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    extra_bytes = 2;
    codepoint = lead & 0x0F;
    min_codepoint = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    extra_bytes = 3;
    codepoint = lead & 0x07;
    min_codepoint = 0x10000;
  } else {
    return std::nullopt;
  }
  if (i + extra_bytes >= size) {
    return std::nullopt;
  }
  for (std::size_t k = 1; k <= extra_bytes; ++k) {
    const unsigned char continuation = data[i + k];
    if ((continuation & 0xC0) != 0x80) {
      return std::nullopt;
    }
    codepoint = (codepoint << 6) | (continuation & 0x3F);
  }
  if (codepoint < min_codepoint || codepoint > 0x10FFFF) {
    return std::nullopt;
  }
  return DecodedCodepoint{codepoint, extra_bytes + 1};
}

// Encodes `codepoint` as UTF-8 bytes, appended to `out`. Permissive of
// an encoded surrogate half (WTF-8-style) so a Genia `\uXXXX` string
// escape naming an unpaired surrogate can be represented at all -- see
// parser.hpp's string-escape handling and json.hpp's decode-side
// `invalid_json_unicode` scalar validation, which is what actually
// rejects it as JSON content.
inline void encode_codepoint(std::uint32_t codepoint, std::string& out) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

}  // namespace genia::utf8
