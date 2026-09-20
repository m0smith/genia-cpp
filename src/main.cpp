// genia-cpp E16-1 adapter entry point.
//
// This binary implements ONLY the E16-1 transport contract: it reads
// exactly one JSON request object from stdin and writes exactly one JSON
// response object to stdout, per
// docs/design/r16-multi-host-conformance-infrastructure-contract.md.
//
// As of E24-2 (m0smith/genia-2026#956) it parses, lowers, and evaluates
// exactly one deliberately minimal vertical slice of Genia source:
// integer literals, bare-name references, and `+ - * /` binary
// expressions, plus `-c` command mode. Every request outside that slice
// -- including every other operation this build predates -- answers
// `unsupported`, and `capabilities` truthfully declares only `parser`,
// `ast_lowering`, `cli_command_mode` (`supported`) and `core_ir_eval`
// (`partial`); every other genia-2026 capability name remains
// `unsupported`. C++ implements Genia, it does not decide what Genia
// means, and it must never claim a capability ahead of real evidence.
//
// Stdout carries the response envelope and nothing else -- no logging, no
// evaluated-program output leaks outside the JSON `result.stdout` field,
// matching the R16 stdout/stderr channel-ownership rule. Any internal
// failure is caught here and results in a clean, empty exit (never a raw
// C++/STL exception reaching the process boundary), so a malformed
// request is classified `protocol_error` by the runner rather than
// `crash`.
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
