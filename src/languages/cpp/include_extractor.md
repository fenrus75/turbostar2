# C/C++ Include Extractor (`languages::cpp::include_extractor`)

## Goals
The `languages::cpp::include_extractor` class (`src/languages/cpp/include_extractor.h`, `src/languages/cpp/include_extractor.cpp`) provides fast and comment-safe extraction of C and C++ `#include` directives:
1. **Directive Parsing**: Extracts header target paths and distinguishes between angle-bracketed system includes (`<vector>`, `<sys/stat.h>`) and double-quoted local project includes (`"foo.h"`).
2. **Comment and String Isolation**: Leverages `languages::cpp::source_masker` so that commented-out `#include` directives (line comments `// #include ...`, block comments `/* #include ... */`, and raw string literals `R"(#include ...)"`) are never falsely parsed as dependencies.
3. **Flexible Call Interfaces**: Exposes span-based (`extract_includes`, `extract_header_names`) and file-based (`extract_includes_from_file`, `extract_header_names_from_file`) overloads with a configurable line cap (default 1000 lines).

## Architecture and Constraints
- **Stateless Analysis**: Operates over read-only line spans or standard files without modifying state or requiring AST compilation databases.
- **Fast CTRE Matching**: Uses Compile-Time Regular Expressions (`ctre::search`) over lexed preprocessor tokens to reliably capture delimiters and header paths with zero runtime regex compilation overhead and zero shared library dependencies.

## Lessons Learned
- **False Dependency Injection via Comments**: Naive regex search in `call_resolver::extract_included_headers` previously searched raw source lines, erroneously treating commented-out header examples and headers inside test strings as active include dependencies. Token-based lexing on active `#include` lines eliminates all false include dependencies.
- **CTRE Migration**: Migrated regex matching from `re2::RE2` to `ctre::search`, eliminating transitive `libabsl_*` dependencies while parsing includes directly with compile-time state machines.
