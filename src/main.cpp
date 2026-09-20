// genia-cpp E16-1 adapter entry point (E24-1: toolchain bootstrap only).
//
// This binary implements ONLY the E16-1 transport contract: it reads
// exactly one JSON request object from stdin and writes exactly one JSON
// response object to stdout, per
// docs/design/r16-multi-host-conformance-infrastructure-contract.md.
//
// It does not parse, lower, or evaluate any Genia source. Every operation
// other than `capabilities` deterministically answers `unsupported`, and
// `capabilities` truthfully declares every genia-2026 capability name as
// `unsupported`. This is intentional and required by E24-1's scope (see
// m0smith/genia-2026#955): C++ implements Genia, it does not decide what
// Genia means, and it must never claim a capability ahead of real
// evidence.
//
// Stdout carries the response envelope and nothing else -- no logging, no
// evaluated-program output, matching the R16 stdout/stderr channel-
// ownership rule. Any internal failure is caught here and results in a
// clean, empty exit (never a raw C++/STL exception reaching the process
// boundary), so a malformed request is classified `protocol_error` by the
// runner rather than `crash`.
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "../third_party/nlohmann_json/json.hpp"
#include "adapter.hpp"
#include "protocol.hpp"

namespace {

std::string read_all_stdin() {
  std::cin >> std::noskipws;
  return std::string(std::istreambuf_iterator<char>(std::cin.rdbuf()),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  try {
    const std::string raw_stdin = read_all_stdin();
    const auto response = genia::adapter::handle_request(raw_stdin);
    if (!response.has_value()) {
      // Malformed/unclassifiable request: write nothing to stdout. The
      // runner's protocol classifier treats non-JSON/empty stdout as
      // protocol_error, which is the correct, honest outcome here --
      // never a fabricated envelope guessing at a case_id we don't have.
      return 0;
    }
    std::cout << genia::protocol::encode_response(*response);
    std::cout.flush();
    return 0;
  } catch (const std::exception&) {
    // Diagnostic-normalization boundary (pre-flight section 5): no
    // internal C++/STL exception text may ever reach stdout or become
    // portable Genia diagnostic output. Since no request could be
    // answered, write nothing and exit cleanly; the runner classifies
    // this as protocol_error, never a crash.
    return 0;
  } catch (...) {
    return 0;
  }
}
