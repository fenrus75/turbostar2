# builtin_signatures

## Overview
`builtin_signatures` provides a fast lookup database of standard C library (ISO C99/C11/C23 POSIX) and C++ standard library (`<algorithm>`, `<utility>`, `<memory>`, `<format>`, `<print>`) function signatures.

## Goals and Constraints
- **Sub-microsecond Lookup**: Uses static hash map for fast query by symbol name, avoiding file reads or LSP overhead for universal standard functions.
- **Prefix Invariance**: Handles both qualified (e.g. `std::min`, `::printf`) and unqualified forms (`min`, `printf`).
- **No Dependencies**: Header-only dependencies (`<string_view>`, `<optional>`).
- **Thread Safety**: Read-only static data, fully thread-safe and re-entrant.

## Lessons Learned
- Users frequently invoke right-click completions on standard library functions like `printf` or `std::format` before writing arguments or when no active LSP is indexing standard system headers. Having instant built-in canonical signatures provides immediate, reliable argument suggestions.
