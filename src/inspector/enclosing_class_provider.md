# `enclosing_class_provider`

## Goals
- Provide instant, zero-latency ambient context (member variables and methods) for the class enclosing the current cursor line.
- Resides at score 34 (`inspector_scores::CLASS_ENCLOSING`), serving as an ambient baseline when moving through class methods.
- Yields seamlessly to the focused symbol/token provider (`class_summary_provider`, score 35), manual pages (score 80), and resolved LSP signatures (score 90).
- Pure synchronous execution leveraging local codemap hierarchy without blocking the UI.

## Constraints
- Operates on 0-based editor line indices, converting to 1-based indices for codemap comparison.
- Never launches background threads; all lookups are fast in-memory operations.
- Thread-safe caching with minimal scoped mutex contention.

## Lessons Learned
- Decoupling ambient scope from focused token inspection ensures that hovering over specific types or variables (e.g. `doc->save()`) promotes cleanly to `document` at score 35 while cursor movement over general statements defaults gracefully to the surrounding class (e.g. `editor`) at score 34.
