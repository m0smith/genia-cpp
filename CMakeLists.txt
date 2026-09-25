cmake_minimum_required(VERSION 3.16)
project(genia_cpp_host LANGUAGES CXX)

# Toolchain policy: docs/design/r24/dependency-toolchain-policy.md
# (genia-2026, pinned at the contract revision in src/protocol.hpp).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# --- genia-adapter: the E16-1 host-adapter binary. As of E24-2 it
# implements one minimal vertical slice (integer literals/arithmetic +
# `-c` command mode); everything else remains deterministically
# unsupported. ---
add_executable(genia-adapter
  src/main.cpp
)
target_include_directories(genia-adapter PRIVATE
  third_party/nlohmann_json
)

# --- Unit tests (Catch2 v2, single vendored header). Internal genia-cpp
# tests only; real conformance evidence comes from genia-2026's
# tools.spec_runner --host against the built genia-adapter binary. ---
enable_testing()
add_executable(genia-adapter-tests
  tests/test_protocol.cpp
  tests/test_bignum.cpp
  tests/test_engine.cpp
  tests/test_diagnostic_boundary.cpp
  tests/test_ref.cpp
  tests/test_cell.cpp
  tests/test_process.cpp
  tests/test_r25_hardening.cpp
)
target_include_directories(genia-adapter-tests PRIVATE
  third_party/nlohmann_json
  third_party/catch2
)
add_test(NAME genia-adapter-tests COMMAND genia-adapter-tests)
