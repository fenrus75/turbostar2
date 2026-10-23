# C/C++ Symbol Extractor (`languages::cpp::symbol_extractor`)

## Goals
The `languages::cpp::symbol_extractor` class (`src/languages/cpp/symbol_extractor.h`, `src/languages/cpp/symbol_extractor.cpp`) provides fast heuristic fallback outline extraction for C and C++ source code lines when language server indexing (LSP / clangd) is unavailable:
1. **Declaration & Definition Detection**: Scans for top-level and scoped functions, classes, structs, and enums using RE2 pattern matching.
2. **Prototype and Specifier Verification**: Scans forward to distinguish forward declarations/prototypes (ending in `;`) from true function/type definitions containing `{ ... }`. Walks backward to capture function specifier lines (`static`, `inline`, `virtual`, `constexpr`, return types).
3. **Scope and Extent Tracking**: Balances curly braces `{ ... }` across source lines to compute accurate `[start_line, end_line]` intervals and line counts for codemap outlines.

## Architecture and Constraints
- **Line-Span Scanning**: Operates over `std::span<const std::string> lines` without taking ownership or performing full-file string allocations.
- **Graceful Cancellation**: Periodically checks `project_manager::get_instance().is_exiting()` to abort immediately on application teardown.
- **Pure / Headless**: Decoupled from editor windows or UI rendering, returning a structured `std::vector<tools::codemap_symbol_info>`.

## Lessons Learned
- **Centralizing C++ Analysis**: Moving C++ symbol outline extraction from `src/codemap_fallbacks.cpp` into `src/languages/cpp/` localizes C++ parsing heuristics to prepare for unified lexing and robust comment/string handling.
