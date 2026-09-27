# call_resolver

## Overall Goals and Architecture
`call_resolver` resolves outgoing call hierarchy targets to their true definition files and factual line bounds.
It handles:
1. Identifying whether a called target is inside the active project vs external/standard library.
2. Disambiguating candidate definition locations returned by LSP or fallback symbol tables.
3. Distinguishing between header declarations and implementation definitions (.h vs .cpp) to accurately attribute targets to where they are actually implemented.
4. Extracting outgoing call references from file slices to populate inline codemap dependency summaries.

## Constraints
- Must avoid false cross-architecture references (e.g. `arch/x86` vs `arch/arm`).
- When a language server is active, unverified external candidates outside the caller's unit must not be guessed.
- Call attribution must respect test code boundaries (test code callers may resolve to tests, but production code callers must not attribute calls to test mock implementations).

## Lessons Learned
- **Dynamic Symbol Attribution in Unit Tests**: Unit tests asserting outgoing call line attribution against real codebase files (such as `src/config_manager.h` or `src/fs_utils.cpp`) must dynamically determine expected start and end lines using `fallback_find_symbols` / `find_symbol_by_hint` rather than asserting static hardcoded line numbers. Normal evolution of the codebase naturally shifts symbol line numbers, which otherwise causes brittle test failures.
- **Header vs Implementation Resolution**: LSP `textDocument/definition` queries often resolve to declaration signatures in `.h` headers instead of `.cpp` implementations. The resolver must upgrade header targets to their companion `.cpp` file when the symbol is implemented there, so codemap summaries direct developers to the executable code.
- **Filtering Enclosing Self-Definitions**: When scanning slice lines for function calls, definition headers of the enclosing function itself match function-call regex patterns. They must be stripped by checking against document symbol names and line ranges to avoid reporting a function as calling its own declaration.
- **Handling Attribute-Decorated Structs and Classes**: Macros and GCC/Clang attributes such as `struct __attribute__((...)) name` contain parentheses prior to `{`. Symbol fallback parsers must account for attributes before class/struct names and prevent attributes like `__attribute__` from matching function patterns, and distinguish `Struct` vs `Class` when expanding symbol bounds.

