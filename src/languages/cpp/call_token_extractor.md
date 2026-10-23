# C/C++ Call Token Extractor (`languages::cpp::call_token_extractor`)

## Goals
The `languages::cpp::call_token_extractor` class (`src/languages/cpp/call_token_extractor.h`, `src/languages/cpp/call_token_extractor.cpp`) provides fast heuristic scanning of C and C++ source code lines to discover candidate function, method, or macro invocation identifiers:
1. **Invocation Pattern Detection**: Detects direct, member, and scoped call expressions matching `ident(...)`, `obj.ident(...)`, `obj->ident(...)`, or `Namespace::ident(...)`.
2. **Control Flow and Builtin Filtering**: Excludes language keywords (`if`, `while`, `for`, `switch`, `return`), cast operators (`static_cast`, `dynamic_cast`), compiler builtins (`__builtin_expect`), and common non-call macros (`likely`, `offsetof`).
3. **Decoupled Architecture**: Exposes pure, headless candidate extraction routines usable by call hierarchy builders, symbol cross-referencing tools, and agent tools.

## Architecture and Constraints
- **Stateless Scanner**: Operates directly over `std::span<const std::string>` line slices with a provided 1-based start line, emitting canonical `languages::source_token` instances.
- **Deduplication**: Maintains a local `std::unordered_set<std::string> seen` to guarantee unique token names in extraction output order.
- **Result Capping**: Halts scanning once a reasonable threshold (default 20 candidates) is reached to prevent unbounded regex processing.

## Lessons Learned
- **Centralizing C++ Analysis**: Moving C++-specific function call regexes and denylists into `src/languages/cpp/` provides a unified home for language analysis routines, allowing `src/call_token_extractor.cpp` to act as a language router.
