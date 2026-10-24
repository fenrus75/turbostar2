# Call Synthesizer (`call_synthesizer`)

## Goals
The `call_synthesizer` class provides contextual code action analysis and intelligent argument call synthesis for C++ code in Turbostar.
Instead of treating right-mouse button clicks as traditional cut/copy/paste menus, `call_synthesizer` inspects the symbol and syntax context at the click position to provide instant argument autocompletions matching in-scope variables, type definitions, or symbol navigation.

## Architecture and Constraints
- **In-Scope Variable Extraction (`extract_in_scope_variables`)**:
  - Traverses the AST / text backwards from the target line to identify the enclosing function boundary and parameter list.
  - Scans local scope declarations (`Type var = ...;`, `auto name = ...;`, `for (...)`) and nearby class member variables (`_` suffix).
  - Collects candidate variables with their declared types and line offsets.
- **Signature Parsing (`parse_function_signature`)**:
  - Parses C++ function signatures and LSP hover prototypes into return types, function names, parameter types, names, and default values.
  - Accurately splits comma-separated parameter lists while respecting template angle brackets (`<...>`).
- **Heuristic Ranking & Synthesis (`generate_argument_suggestions`)**:
  - Scores candidate variables against each signature parameter using fuzzy name matches (stripping prefixes/suffixes/case) and type compatibility checks.
  - Synthesizes top-ranked call suggestions (`(arg1, arg2, ...)`), falling back to idiomatic defaults (`0`, `1`, `""`, `false`) when no matching local variable exists.
- **Fixed Slot Principle & Multi-Tier Resolution**:
  - Tier 1 (Native C++): Generates 1–2 native heuristic suggestions with microsecond latency (< 10ms).
  - Tier 2 (Asynchronous AI): Reserves a stable slot in the popup menu for LLM one-shot synthesis, updating in-place without shifting menu coordinates or hotkeys under the mouse.
- **The Three Call Site Syntactic Cases**:
  - **Case 1: Naked Identifier (`func`)**: Cursor is on or after a function name with no following `(`. `codemap_utils` resolves the signature via built-ins or LSP/cache. Suggested calls append `(args...)` after the token.
  - **Case 2: Half Call (`func(` or `func(arg1, `)**: Cursor is at an unclosed call site. Partially typed arguments are preserved, and remaining parameters are synthesized. The selection replaces from `(` through the unclosed boundary.
  - **Case 3: Full Call (`func(a, b)`)**: Cursor is on or within balanced parentheses. Alternative variable assignments or refactorings replace the entire `(...)` argument list in place.
- **Resolution & Caching Hub (`codemap_utils`)**:
  - Centralizes signature and definition lookups with `languages::cpp::builtin_signatures` for instant standard library resolution and a thread-safe in-memory cache for microsecond repeat lookups.

## Lessons Learned
- **UI Stability Over In-Place Mutation**: Modifying or shifting menu options while a user is aiming to click causes misclicks and user frustration. Reserving fixed slots in the popup menu ensures that native options remain pinned at slots `1` and `2`, while background AI refinements cleanly populate their designated slot without layout shifts.
- **Template-Aware Parameter Tokenization**: Splitting parameter lists by raw commas fails on complex templates (e.g. `std::vector<int, std::allocator<int>>`). Tracking template bracket depth during tokenization prevents malformed parameter types.
- **Naked and Incomplete Call Site Handling**: Right-clicking before parentheses are typed (naked identifier) or while midway through typing arguments (half call) requires decoupled token discovery: scanning backwards to the enclosing call if inside parens, querying built-ins and cache when LSP hasn't yet indexed new code, and applying distinct text insertions vs range replacements depending on the call site state.
