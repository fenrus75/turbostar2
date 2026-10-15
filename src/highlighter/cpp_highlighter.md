# C++ Syntax Highlighter (`cpp_highlighter`)

## Overview & Goals
The `cpp_highlighter` class applies C and C++ syntax highlighting to individual editor `line` objects. It identifies preprocessor directives, double-quoted strings, single-quoted character literals, single-line block comments (`/* ... */`), line comments (`// ...`), and C++ keywords.

## Key Responsibilities
- **File & Language Matching**: Recognizes file extensions `.cpp`, `.h`, `.hpp`, `.c`, `.cc`, `.cxx` and language identifiers `cpp`, `c`, `c++`, `cc`, `h`, `hpp`, `cxx`.
- **Character Offset Mapping**: Accurately maps byte matches from regular expressions to Unicode character indices using `utf8::byte_to_char_pos`.
- **Precedence Order**: Preprocessor -> Strings -> Character Literals -> Block Comments -> Line Comments -> Keywords (only where attributes are currently `normal`).

## Constraints & Security Considerations
- **Untrusted Source Text**: Input lines may contain arbitrary characters, embedded null bytes, malformed UTF-8, or deceptive syntax constructs.
- **Bounds Checking**: Every attribute assignment verifies `char_idx < attrs.size()`.
- **Keyword Isolation**: Keywords inside string literals or comments must never be highlighted as keywords.

## Lessons Learned
- **Multiple `//` Sequences on a Single Line**: When searching for line comments, calling `text.find("//")` once halts on the first occurrence. If a string or block comment earlier on the line contains `"//"` (e.g. `const char* url = "http://example.com"; // comment`), a single search skips marking the actual line comment. Iterating with `search_pos = comment_pos + 2` until finding a `//` outside of strings/comments correctly locates the trailing comment.
