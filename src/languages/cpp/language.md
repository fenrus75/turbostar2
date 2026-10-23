# C/C++ Language Utilities (`languages::cpp`)

## Goals
The `languages::cpp` utility namespace (`src/languages/cpp/language.h`, `src/languages/cpp/language.cpp`) centralizes file and language identification predicates for C and C++ source code:
1. **Single Source of Truth**: Unifies file extension checks (`.c`, `.cpp`, `.cc`, `.cxx`, `.h`, `.hpp`) across highlighters, fallback symbol extractors, call resolvers, and type definition tokenizers.
2. **Language Specifier Matching**: Identifies recognized language alias strings (`c`, `cpp`, `c++`, `cc`, `h`, `hpp`, `cxx`) case-insensitively.

## Architecture and Constraints
- **Pure Functions**: `supports_file` and `supports_language` are `noexcept` non-throwing utility functions operating over `std::string_view`.
- **Zero Allocations on Lookup**: Case conversions use existing UTF-8 helpers.

## Lessons Learned
- **Centralizing Extension Checks**: Multiple components (`cpp_highlighter`, `codemap_fallbacks`, `type_token_extractor`) previously checked file extensions independently or omitted them entirely, leading to C++ heuristics being executed on non-C++ files.
