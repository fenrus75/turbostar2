# `lsp_inspector_provider`

## Goals
- Provide rich Language Server Protocol contextual data (function prototypes, parameter suggestions, declaration links) to the companion inspector window.
- Execute quick local resolution (< 1ms) if syntax context was already parsed in active documents.
- Fall back gracefully to background asynchronous resolution (`query_async`) for external or uncached symbols without blocking the UI thread.

## Constraints
- Must not perform blocking network or LSP requests on the main thread during `query()`.
- Thread-safe async execution via `query_async`.
- Generates clean Markdown Lite using `inspector::markdown_builder`.

## Lessons Learned
- Fast-path checking for local context via `call_synthesizer::analyze_context_at` allows immediate score 90 rendering for symbols defined in the current file or translation unit, completely bypassing background thread dispatch when data is already available.
