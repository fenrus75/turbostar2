# @@PROJECT_NAME@@ Development Guidelines

## Build & Test Instructions
- **Configure**: `meson setup build`
- **Compile**: `ninja -C build`
- **Run**: `./build/@@EXECUTABLE_NAME@@`
- **Test**: `meson test -C build`

## Code Conventions
- **Language Standard**: @@LANGUAGE_STD@@
- **Include Guards**: Prefer `#pragma once`.
- **Memory Management**: Follow RAII strictly; avoid raw `new` and `delete`, using `std::make_unique` and `std::make_shared`.
- **String & Sequence Handling**: Prefer `std::string_view` for read-only string function parameters over `const std::string &`, and `std::span<const T>` for read-only contiguous sequence parameters over `const std::vector<T> &` to avoid unnecessary heap allocations; use `std::string` / `std::vector` when ownership or mutation is required. Avoid raw `char *` except when interfacing with C APIs or the OS kernel.
- **Standard Library**: Prefer C++ Standard Library containers and algorithms over custom implementations.
- **File Organization**: Place each class in a dedicated `.cpp` file and matching header in the same directory.
- **Constexpr & Qualifiers**: Label methods and parameters `constexpr`, `const`, `std::string_view`, `std::span`, and `noexcept` when appropriate.

## Security Considerations
- **Security-First**: Adopt a security-first mindset.
- **Untrusted Input**: Clearly label variables and parameters holding untrusted (user/network) data in comments.
- **Early Validation**: Sanity-check and validate all untrusted input as early as possible.
- **Bounds Checking**: Verify all buffer and array accesses against underflows and overflows.

## Code Commenting Conventions
- **Intent over Logic**: Focus comments on rationale (the "why") and design goals rather than restating code logic.
- **Ownership & Constraints**: Document ownership, thread-safety expectations, and function constraints.
- **Mutex Declarations**: Precede every mutex declaration in header files with a comment block explaining:
  1. What specific data or resources the mutex protects.
  2. Locking rules, lifecycle, or ordering guidelines.

## Version Control Conventions
- **Pre-commit Review**: Perform a code review before each commit to ensure no stray or accidental edits are included.
- **Test Verification**: Run the full test suite before committing changes.