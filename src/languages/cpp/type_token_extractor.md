# C/C++ Type Token Extractor (`languages::cpp::type_token_extractor`)

## Goals
The `languages::cpp::type_token_extractor` class (`src/languages/cpp/type_token_extractor.h`, `src/languages/cpp/type_token_extractor.cpp`) provides fast heuristic scanning of C and C++ source code lines to discover candidate type identifiers (classes, structs, enums, typedefs) near an active cursor position, crash point, or inspection range:
1. **Structural Type Heuristics**: Detects type usage patterns including explicit declarations (`class X`, `struct Y`), scope resolution prefixes (`X::member`), template arguments (`<Type>`), pointer/reference parameters (`Type *ptr`, `const Type &ref`), and variable definitions (`Type var;`).
2. **Noise and Keyword Filtering**: Filters out language keywords, primitive types (`int`, `void`, `size_t`), standard library containers (`std::string`, `vector`, `optional`), external library namespaces (`nlohmann::json`, `re2`), and ALL_CAPS macro constants.
3. **Decoupled Architecture**: Exposes pure, headless candidate extraction routines usable by symbol indexers, cache managers, and agent tools without GUI or document dependencies.

## Architecture and Constraints
- **Stateless Scanner**: Operates directly over `std::span<const std::string>` line slices with a provided 1-based start line, emitting canonical `languages::source_token` instances.
- **Deduplication**: Maintains a local `std::unordered_set<std::string> seen` to guarantee unique token names in extraction output order.
- **Result Capping**: Halts scanning once a reasonable threshold (default 15 candidates) is reached to prevent unbounded regex processing on massive files.

## Lessons Learned
- **Centralizing C++ Analysis**: Moving C++-specific type candidate regexes and denylists into `src/languages/cpp/` provides a unified home for language analysis routines, allowing `src/type_token_extractor.cpp` to act as a language router.
- **Comment and Literal Masking**: Previously, types in trailing comments (`// struct Type`) or string literals (`"class Type"`) were falsely extracted. Integrating `languages::cpp::source_masker` replaces comments and literals with column-preserving whitespace, eliminating false type tokens while keeping exact line and column locations intact.
