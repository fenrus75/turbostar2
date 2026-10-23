# C/C++ Source Masker (`languages::cpp::source_masker`)

## Goals
The `languages::cpp::source_masker` class (`src/languages/cpp/source_masker.h`, `src/languages/cpp/source_masker.cpp`) provides fast, column-preserving masking of comments and string/character literals in C and C++ source code lines:
1. **Length & Column Invariance**: Replaces characters inside comments (`//`, `/* ... */`) and literals (`"..."`, `'...'`, `R"(...)"`) with space characters (`' '`), preserving the exact length and column offsets of all active code identifiers.
2. **Multi-line State Tracking**: Robustly tracks cross-line block comments and raw string literals via `languages::cpp::mask_state`.
3. **Prefix & Escape Awareness**: Correctly parses escaped quotes (`\"`, `\'`, `\\`), raw string custom delimiters (`R"delim(...)delim"`), and string/character encoding prefixes (`u8R"`, `L"`, etc.).

## Architecture and Constraints
- **Stateless Batch & Stateful Line Interfaces**: Exposes `mask_lines(std::span<const std::string>)` for batch document slicing and `mask_line(std::string_view, mask_state &)` for iterative line streams.
- **Zero Syntax Alteration Outside Literals**: Leaves code braces (`{`, `}`), parentheses (`(`, `)`), keywords, and identifiers untouched at their original coordinates.
- **Byte and Character Count Invariance**:
  - The masker guarantees **both exact byte-for-byte length and 1:1 Unicode character count invariance** across lines.
  - Multi-byte UTF-8 sequences inside comments and literals are replaced with valid UTF-8 whitespace of the exact same byte length:
    - 1-byte sequences $\to$ ASCII space `0x20`
    - 2-byte sequences $\to$ `U+00A0` No-Break Space (`\xC2\xA0`)
    - 3-byte sequences $\to$ `U+2000` En Quad (`\xE2\x80\x80`)
    - 4-byte sequences $\to$ `U+F0020` Plane 15 Private Use Area whitespace (`\xF3\xB0\x80\xA0`)
  - All replacement characters are recognized as whitespace by `utf8::is_space_at_byte` and `utf8::is_space_at_char`.
  - This ensures that both byte offsets (used by RE2 regex and pointer arithmetic) and character positions (`utf8::byte_to_char_pos`) remain 100% stable without drift.

## Lessons Learned
- **Brace & Token Corruption**: Previous token extractors and fallback symbol finders did not mask comments and strings, causing braces or function call patterns inside comments and string literals to corrupt AST depths or emit false candidate tokens.
- **Byte Offset Guarantees**: Maintaining exact byte offsets is essential because C++ regex scanners calculate `col` via byte pointer offsets (`submatch.data() - line.data()`). Byte-level space filling keeps these offsets 100% stable without shifting downstream token coordinates.
