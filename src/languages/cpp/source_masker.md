# C/C++ Source Masker (`languages::cpp::source_masker`)

## Goals
The `languages::cpp::source_masker` class (`src/languages/cpp/source_masker.h`, `src/languages/cpp/source_masker.cpp`) provides fast, column-preserving masking of comments and string/character literals in C and C++ source code lines:
1. **Length & Column Invariance**: Replaces characters inside comments (`//`, `/* ... */`) and literals (`"..."`, `'...'`, `R"(...)"`) with space characters (`' '`), preserving the exact length and column offsets of all active code identifiers.
2. **Multi-line State Tracking**: Robustly tracks cross-line block comments and raw string literals via `languages::cpp::mask_state`.
3. **Prefix & Escape Awareness**: Correctly parses escaped quotes (`\"`, `\'`, `\\`), raw string custom delimiters (`R"delim(...)delim"`), and string/character encoding prefixes (`u8R"`, `L"`, etc.).

## Architecture and Constraints
- **Stateless Batch & Stateful Line Interfaces**: Exposes `mask_lines(std::span<const std::string>)` for batch document slicing and `mask_line(std::string_view, mask_state &)` for iterative line streams.
- **Zero Syntax Alteration Outside Literals**: Leaves code braces (`{`, `}`), parentheses (`(`, `)`), keywords, and identifiers untouched at their original coordinates.
- **Byte Invariance vs. Character/Display Position**:
  - The masker guarantees **exact byte-for-byte length and offset invariance** across lines by replacing each byte inside comments and literals with a single ASCII `' '` byte.
  - Multi-byte UTF-8 sequences (such as emojis or accented characters) inside comments/literals become an equivalent number of ASCII space bytes (e.g., a 4-byte emoji becomes 4 space bytes).
  - This preserves byte offsets required by RE2 regex matching and pointer arithmetic (`match.data() - line.data()`), rather than visual display width or Unicode code point count.
  - If character-position invariance is ever required in the future, multi-byte UTF-8 characters can be substituted with multi-byte whitespace characters matching the exact byte length.

## Lessons Learned
- **Brace & Token Corruption**: Previous token extractors and fallback symbol finders did not mask comments and strings, causing braces or function call patterns inside comments and string literals to corrupt AST depths or emit false candidate tokens.
- **Byte Offset Guarantees**: Maintaining exact byte offsets is essential because C++ regex scanners calculate `col` via byte pointer offsets (`submatch.data() - line.data()`). Byte-level space filling keeps these offsets 100% stable without shifting downstream token coordinates.
