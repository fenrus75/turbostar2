# @@PROJECT_NAME@@ Development Guidelines

## Build & Test Instructions
- **Configure**: `meson setup build`
- **Compile**: `ninja -C build`
- **Run**: `./build/@@EXECUTABLE_NAME@@`
- **Test**: `meson test -C build`

## Code Conventions
- **Language Standard**: @@LANGUAGE_STD@@
- **Include Guards**: Prefer `#pragma once` or standard `#ifndef` include guards for header files.
- **Const Correctness**: Use `const` for read-only pointer arguments (`const char *`, `const uint8_t *`).
- **Memory Management**: Check return values of `malloc`/`calloc`/`realloc`. Ensure every allocated resource is freed and pointers cleared (`ptr = NULL`).
- **Safe String Functions**: Avoid unbounded string functions (`strcpy`, `sprintf`, `gets`); use size-bounded functions (`snprintf`, `strncpy`) and guarantee null-termination.
- **Initialization**: Always initialize variables and struct instances (e.g. `{0}` or `memset`) prior to use.
- **File Organization**: Place each module/component in a dedicated `.c` file and matching `.h` header in the same directory.

## Security Considerations
- **Security-First**: Adopt a security-first mindset.
- **Untrusted Input**: Clearly label variables and parameters holding untrusted (user/network) data in comments.
- **Early Validation**: Sanity-check and validate all untrusted input as early as possible.
- **Bounds Checking**: Verify all buffer and array accesses against underflows and overflows.
- **Buffer Size Parameters**: Always pass explicit buffer size parameters (`size_t buf_size`) alongside pointer arguments.

## Code Commenting Conventions
- **Intent over Logic**: Focus comments on rationale (the "why") and design goals rather than restating code logic.
- **Ownership & Constraints**: Document pointer ownership, memory lifecycles, thread-safety expectations, and function constraints.
- **Mutex Declarations**: Precede every mutex declaration in header files with a comment block explaining:
  1. What specific data or resources the mutex protects.
  2. Locking rules, lifecycle, or ordering guidelines.

## Version Control Conventions
- **Pre-commit Review**: Perform a code review before each commit to ensure no stray or accidental edits are included.
- **Test Verification**: Run the full test suite before committing changes.