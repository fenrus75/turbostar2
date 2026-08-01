# Modern C++23 Coding Guidelines

## Core Principles
- **Standard Version**: TurboStar uses C++23. Use modern language features (`std::format`, `std::string_view`, `std::span`, `constexpr`, `noexcept`, structured bindings) whenever appropriate.
- **Memory & Resource Management**: Follow RAII strictly. Avoid raw `new` and `delete`; favor `std::make_unique` and `std::make_shared`.
- **String & Sequence Parameters**: Prefer `std::string_view` for read-only string parameters over `const std::string &`. Prefer `std::span<const T>` for read-only contiguous sequence parameters over `const std::vector<T> &` to avoid unnecessary allocations.
- **Formatting**: Prefer `std::format` over raw C string concatenations or `sprintf`.
- **Include Paths**: All `#include ""` directives must be relative to the `src/` directory (e.g., `#include "fs_utils.h"` instead of relative `../../` paths).
- **Include Guards**: Use `#pragma once` for all header files.

## Concurrency & Mutexes
- **Mutex Documentation**: When declaring a mutex in a header file, you MUST add a comment block immediately preceding the declaration explaining:
  1. What specific member data or resources the mutex protects.
  2. The general locking rules, lifecycle, or ordering guidelines associated with it.

## Unit Test Guidelines
- **Test Isolation**: Every unit test MUST isolate the `HOME` environment variable to a temporary directory to prevent race conditions during parallel test execution. Call `test_watchdog::setup_watchdog()` at the start of `main()`.
