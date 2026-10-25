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
- Differentiating system headers and missing paths: When symbols resolve to system headers (e.g. libc or POSIX headers in `/usr/`) or when LSP cannot determine an actual file path (`declaration_location` is empty, `<built-in>`, or non-path), the LSP signature is scored at 70 (`LSP_SYSTEM_HEADER` / `LSP_LOW_CONFIDENCE`). This allows authoritative man page documentation (score 80) to win and present clean prototypes with human-readable summaries, while keeping verified project-local symbols at score 90 (`LSP_RESOLVED`).
