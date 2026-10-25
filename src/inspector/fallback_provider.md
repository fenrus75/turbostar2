# `fallback_inspector_provider`

## Goals
- Serve as the baseline fallback provider for the Symbol Inspector companion window.
- Provide immediate zero-latency feedback (score 15) echoing the current token, or an idle notice (score 0) when the cursor is over whitespace.
- Act as the floor tier in provider ranking so the window is never blank when a word is under the cursor.

## Constraints
- Never blocks; execution time must be under 0.01ms.
- 100% thread-safe and stateless.

## Lessons Learned
- Returning a low non-zero score (15) guarantees that while higher-ranked providers (such as LSP or background linters) resolve asynchronously, the user immediately sees the active token under the cursor without visual flashes.
