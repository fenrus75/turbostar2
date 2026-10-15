# Python Syntax Highlighter (`python_highlighter`)

## Overview & Goals
The `python_highlighter` parses and styles Python source files (`.py`, `.pyw`). It recognizes keywords, string literals (double and single quoted), and comments (`#`).

## Key Responsibilities
- **File & Language Matching**: Recognizes `.py`, `.pyw` and languages `python`, `py`.
- **Keyword Matching**: Matches Python keywords (`def`, `class`, `return`, `import`, `if`, `else`, etc.) via RE2 regex.
- **String Literals**: Matches single-line double-quoted and single-quoted strings with escape sequence handling.
- **Comments**: Highlights trailing comments starting with `#`.

## Constraints & Security Considerations
- **Untrusted Source Code**: Input may contain nested quotes, format strings, or `#` characters inside string literals.
- **Bounds Checking**: Enforces `char_pos < attrs.size()` on all style applications.

## Lessons Learned
- **Multiple `#` Characters on One Line**: A single `text.find('#')` search stops at the first `#`. When code contains `#` inside a string literal (e.g. `color = "#ff0000" # comment`), the actual line comment was previously omitted. Iterating through occurrences of `#` until finding one outside string literals guarantees comments are recognized.
