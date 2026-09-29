// Per-run process I/O state for the R27 E27-1 Flow phase 1 slice: the
// captured stdout text written by `print`, and the stdin source that
// `stdin |> lines` adapts into a Flow.
//
// Both are host-local plumbing, not Genia semantics. What is observable
// (and pinned by shared `spec/flow/*` evidence) is only the resulting
// stdout/stderr/exit_code, so the evaluator appends `print` output here
// and the engine prepends it to the final-value display.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace genia::runtime_io {

// Shared read position over stdin's lines. `GeniaStdinSource` in the
// reference host caches one iterator, so two `stdin |> lines` Flows share
// one position rather than each re-reading from the start.
struct StdinState {
  std::vector<std::string> lines;
  size_t position = 0;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local std::string g_stdout_text;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local std::shared_ptr<StdinState> g_stdin;
// File name used in pipeline-stage error spans (`<command>` for command-source
// execution); empty when the reference host's file naming is not evidenced.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline thread_local std::string g_source_name;

// Splits `text` into stdin lines the way the reference host's text-mode
// iteration does for `\n`-terminated input: each line loses its trailing
// `\r`/`\n` characters and a final unterminated line still counts. Returns
// std::nullopt when `text` contains a carriage return: universal-newline
// translation of `\r` is not evidenced by any shared case, so it is left
// unsupported rather than guessed.
inline std::optional<std::shared_ptr<StdinState>> make_stdin(const std::string& text) {
  auto state = std::make_shared<StdinState>();
  if (text.find('\r') != std::string::npos) return std::nullopt;
  size_t start = 0;
  while (start < text.size()) {
    const size_t newline = text.find('\n', start);
    if (newline == std::string::npos) {
      state->lines.push_back(text.substr(start));
      break;
    }
    state->lines.push_back(text.substr(start, newline - start));
    start = newline + 1;
  }
  return state;
}

// Installs a fresh stdout buffer and stdin state for one run and restores
// the previous ones afterwards (the evaluator may be entered re-entrantly by
// unit tests).
struct RunGuard {
  std::string previous_stdout;
  std::shared_ptr<StdinState> previous_stdin;
  std::string previous_source_name;
  RunGuard(std::shared_ptr<StdinState> stdin_state, std::string source_name)
      : previous_stdout(std::move(g_stdout_text)),
        previous_stdin(std::move(g_stdin)),
        previous_source_name(std::move(g_source_name)) {
    g_stdout_text.clear();
    g_stdin = std::move(stdin_state);
    g_source_name = std::move(source_name);
  }
  ~RunGuard() {
    g_stdout_text = std::move(previous_stdout);
    g_stdin = std::move(previous_stdin);
    g_source_name = std::move(previous_source_name);
  }
  RunGuard(const RunGuard&) = delete;
  RunGuard& operator=(const RunGuard&) = delete;
  RunGuard(RunGuard&&) = delete;
  RunGuard& operator=(RunGuard&&) = delete;
};

}  // namespace genia::runtime_io
