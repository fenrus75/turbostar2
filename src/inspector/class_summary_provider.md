# `class_summary_provider`

## Goals
- Provide ambient class summaries (key member variables and member functions) for the class enclosing the cursor or named by the active token when specific function signatures or man pages are unavailable.
- Seamlessly adapt output to the available vertical lines in the companion window (`q.available_lines`: 2, 3, or 4 lines).
- Display the class name and originating header path in the companion window title bar via `title_addon` (e.g. `class document (src/document.h)`), freeing content lines for fields and methods.
- Serve as an informative middle tier in the inspector scoring hierarchy (score 35: `inspector_scores::CLASS_SUMMARY`), prioritizing above generic token fallbacks (15–20) while gracefully yielding to authoritative manual pages (80) and resolved LSP signatures (90).
- Employ in-memory caching for sub-microsecond cache hits and negative caching for fast rejection.

## Constraints
- Never blocks the UI thread: file checks and cache lookups are non-blocking synchronously, while AST parsing, LSP document symbols, and fallback brace scanning run in detached background threads.
- Decoupled from ncurses or terminal APIs; formats pure Markdown Lite using `inspector::markdown_builder`.
- Uses brief, scoped mutex protection on the internal lookup cache without holding locks across LSP or disk operations.

## Lessons Learned
- Vertical line budgeting: In 2-line mode, omit the class header line and allocate both lines to member variables and functions to maximize density. In 3-line and 4-line modes, surface the class name with member counts on line 1 and allocate subsequent lines to fields, methods, and overflow methods.
- Dual C++ source/header support: Queries originating from both implementation files (`.cpp`) and header files (`.h`) correctly locate the class definition—headers parse directly, while implementation files resolve the header via LSP definition query or matching file conventions.
