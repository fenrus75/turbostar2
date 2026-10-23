# C/C++ Lightweight Lexer (`languages::cpp::lexer`)

## Goals
The `languages::cpp::lexer` class (`src/languages/cpp/lexer.h`, `src/languages/cpp/lexer.cpp`) provides a fast, zero-allocation, error-resilient tokenizer for C and C++ source code:
1. **Lightweight Token Stream**: Converts C++ source text into a flat sequence of typed tokens (`identifier`, `keyword`, `number`, `string_literal`, `character_literal`, `comment`, `preprocessor`, punctuation, operators) without building a heavy AST.
2. **Error Resilience & Fault Tolerance**: Designed specifically for code editors where code is often incomplete, in-progress, or syntactically invalid:
   - Extra closing braces (`}`), parentheses (`)`), or brackets (`]`) when depth is 0 are clamped to 0 rather than underflowing or halting tokenization.
   - Unclosed string literals or character literals recover cleanly at line boundaries.
   - Unclosed block comments (`/* ...`) recover cleanly at EOF.
   - Raw string literals (`R"delim(...)delim"`) are parsed with custom delimiter extraction and fallback gracefully if unclosed.
3. **Delimiter Depth Tracking**: Directly stamps `brace_depth`, `paren_depth`, and `bracket_depth` onto each token so consumers (symbol extractors, call resolvers, highlighters) do not need to repeat error-prone nesting calculations.

## Architecture and Constraints
- **Zero-Copy Token Slices**: All `token.text` fields are `std::string_view` referencing the caller's contiguous source buffer, avoiding memory allocations during tokenization.
- **Unified Keyword Classification**: Integrates directly with `languages::cpp::keywords::is_keyword(...)` to distinguish standard C++ keywords and primitive types from general identifiers.
- **Graceful EOF Handling**: Always terminates with a sentinel `token_kind::eof` token.

## Lessons Learned
- **Clamping Depth Underflows**: In malformed source files (e.g. stray `}` or copy-paste errors), a naive depth counter decrements into negative values, causing all subsequent top-level declarations to be misidentified as nested members. Clamping depths with `std::max(0, depth - 1)` ensures that stray closing delimiters never corrupt the remainder of the file.
- **Preprocessor Line Isolation**: Treating `#`-prefixed preprocessor directives as distinct preprocessor tokens isolates macros like `#define FUNC(a, b)` and `#include <header>` from regular function declaration patterns, preventing false positives in symbol extractors.
