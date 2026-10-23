# C/C++ Keywords and Identifier Classifier (`languages::cpp::keywords`)

## Goals
The `languages::cpp::keywords` class (`src/languages/cpp/keywords.h`, `src/languages/cpp/keywords.cpp`) provides a centralized, single-source-of-truth database and classifier for C and C++ keywords, primitive types, function specifiers, and AST token filter sets:
1. **Keyword Identification**: Accurately classifies standard C/C++ keywords (including modern C++20/C++23 keywords such as `consteval`, `constinit`, `concept`, `requires`, `co_await`, `co_return`, `co_yield`) and alternative operator tokens.
2. **Primitive & Standard Integer Classification**: Identifies fundamental types (`void`, `int`, `double`, `char32_t`) and standard fixed-width/pointer typedefs (`size_t`, `ptrdiff_t`, `intptr_t`, `uint64_t`).
3. **Specifier Analysis**: Powers backward specifier scanning in `languages::cpp::symbol_extractor` to correctly capture multi-line function declarations and return types.
4. **Token Extractor Denylists**: Eliminates duplicated ad-hoc sets across `call_token_extractor` and `type_token_extractor`.

## Architecture and Constraints
- **Zero-Allocation Queries**: Backed by static sets initialized on first access, querying via `std::string_view` without heap allocation.
- **Pure and Headless**: Fully decoupled from document objects, regex engines, or editor state.
- **Hierarchical Classification**: `is_type_candidate_denylisted` cleanly composes `is_primitive_type`, `is_keyword`, `is_std_type_or_container`, noise namespaces, and uppercase macro checks.

## Lessons Learned
- **Ad-Hoc Drifts and Missing Items**: Previously, `call_token_extractor.cpp`, `type_token_extractor.cpp`, and `symbol_extractor.cpp` each maintained separate, diverging lists of keywords and types. As a result:
  - `symbol_extractor` was missing `ptrdiff_t`, `char32_t`, and `constinit`, causing multi-line function headers to report incorrect `start_line` values.
  - `call_token_extractor` was missing `co_await`, `alignas`, and `concept`, causing keywords with parentheses to be falsely extracted as function calls.
  - `type_token_extractor` was missing `ptrdiff_t`, `uintptr_t`, and `nullptr_t`, causing primitive aliases to be returned as custom user types.
- Centralizing these definitions in `languages::cpp::keywords` ensures fixes immediately benefit all parsers, extractors, and syntax highlighters simultaneously.
