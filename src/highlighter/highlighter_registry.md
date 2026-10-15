# Syntax Highlighter Registry (`highlighter_registry`)

## Overview & Goals
The `highlighter_registry` is a singleton manager responsible for discovering, instantiating, and dispatching syntax highlighters based on filenames or markdown language identifiers.

## Key Responsibilities
- **Singleton Access**: Provides a global instance via `get_instance()`.
- **File Dispatch (`get_highlighter_for_file`)**: Evaluates registered highlighters in order and returns the first matching highlighter, falling back to `default_highlighter` (which leaves lines in normal syntax attributes).
- **Language Dispatch (`get_highlighter_for_language`)**: Matches language identifiers (e.g. for markdown fenced code blocks) or returns `nullptr` if unsupported.

## Constraints & Security Considerations
- **Fallback Safety**: `get_highlighter_for_file` never returns `nullptr`, preventing null dereference crashes in editing loops.
- **Case Sensitivity**: Filename extensions are evaluated by individual highlighters, with case-insensitive comparisons used where appropriate.

## Lessons Learned
- **Highlighter Order Matters**: Highlighters are evaluated in vector registration order; general highlighters must not shadow more specific language rules.
