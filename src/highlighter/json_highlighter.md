# JSON Syntax Highlighter (`json_highlighter`)

## Overview & Goals
The `json_highlighter` colors JSON (JavaScript Object Notation) and JSONC documents. It differentiates object keys, string values, numbers, booleans (`true`, `false`), `null`, punctuation brackets/braces/commas, and single-line comments (`//`).

## Key Responsibilities
- **File & Language Matching**: Recognizes `.json` and language `json`.
- **Key vs Value Distinction**: Recognizes `("key")\s*:` patterns and highlights keys as keywords, separating them from string values.
- **Literal Coloring**: Highlights numbers as italic, keys and booleans as keywords, strings as string literals, and punctuation (`{`, `}`, `[`, `]`, `,`, `:`) as headings.

## Constraints & Security Considerations
- **Untrusted JSON Content**: Lines may contain deeply nested brackets, escaped quotes, URLs with `//`, or arbitrary Unicode content.
- **Precedence Order**: String literals (keys and values) MUST be parsed before line comments (`//`) to prevent URLs (like `https://...`) inside string literals from turning the remainder of the line into a comment.

## Lessons Learned
- **Comment Parsing Precedence**: Running `text.find("//")` before parsing strings causes any string with `//` (such as URLs in `{ "url": "https://..." }`) to erroneously color the URL and subsequent fields as a comment. Parsing keys and string values first, then iterating for `//` that occurs outside string literals and keys, resolves this issue.
- **CTRE Migration**: Migrated regex matching to `ctre::search_all`, eliminating heap-allocated `std::unique_ptr<re2::RE2>` instances and avoiding Abseil dependencies while compiling all JSON patterns into static state machines.
