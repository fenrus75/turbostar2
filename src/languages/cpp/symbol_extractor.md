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
- **Comment and Literal Masking**: Unbalanced braces `{` or `}` inside string literals or comments corrupted AST brace depth tracking and caused false symbol outlines (e.g. `class FakeClass` inside a comment). Pre-masking with `languages::cpp::source_masker` blanks comments and literals with byte- and character-invariant whitespace, preventing syntax pollution and ensuring reliable `[start_line, end_line]` intervals.
- **Destructors, Operators & Templates**: Enhanced `cpp_func_head_regex` to match destructors (`~ClassName`) and operator overloads (`operator==`, `operator[]`, etc.), and added `template`/`typename`/`concept` to `keywords::is_function_specifier` so multi-line template function definitions correctly report their leading `template <...>` line as `start_line`.
- **Namespace & Class Hierarchical Scoping**: Implemented multi-pass scope extraction (`namespace_scope` and `type_scope`) that tracks nested, compact C++17 (`namespace foo::bar`), and anonymous (`(anonymous namespace)`) namespaces while skipping namespace aliases (`namespace fs = ...;`) and `using namespace` directives. Qualifies all child types, enums, inline methods, and out-of-line methods with their enclosing scopes and accurate nesting depths.
