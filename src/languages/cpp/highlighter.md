# C/C++ Syntax Highlighter (`languages::cpp::highlighter`)

## Goals
The `languages::cpp::highlighter` class (`src/languages/cpp/highlighter.h`, `src/languages/cpp/highlighter.cpp`) applies C and C++ syntax highlighting to individual editor `line` objects:
1. **Preprocessor Directives**: Highlights `#include`, `#define`, `#pragma`, and related directives as keywords.
2. **String and Character Literals**: Classifies double-quoted strings and single-quoted character constants with escape sequence support.
3. **Comments**: Identifies single-line block comments (`/* ... */`) and line comments (`// ...`) safely without colliding with strings (e.g. `"http://"`).
4. **Keywords and Types**: Highlights standard C/C++ keywords, primitives, and standard library types by consulting the centralized `languages::cpp::keywords` classifier.

## Architecture and Constraints
- **Subclass of `syntax_highlighter`**: Implements `supports_file(filename)`, `supports_language(lang)`, and `highlight(line)`.
- **Integrated with `languages::cpp`**: Leverages `languages::cpp::supports_file` / `supports_language` and `languages::cpp::keywords`.
- **Precedence Order**: Preprocessor $\to$ Strings $\to$ Chars $\to$ Block Comments $\to$ Line Comments $\to$ Keywords (only over unmarked `syntax_attribute::normal` spans).

## Lessons Learned
- **Centralizing C++ Logic**: Previously, C++ syntax highlighting was isolated in `src/highlighter/cpp_highlighter.cpp` with a hardcoded, brittle keyword regex. Moving to `src/languages/cpp/highlighter.cpp` and wiring it to `languages::cpp::keywords` ensures fixes to keyword or type definitions immediately propagate to editor syntax coloring.
