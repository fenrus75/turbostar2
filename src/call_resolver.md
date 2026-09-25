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
