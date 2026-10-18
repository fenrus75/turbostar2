# LSP Manager (`lsp_manager`)

## Goals
The `lsp_manager` class (`src/lsp_manager.h`, `src/lsp_manager.cpp`) serves as the central high-level facade for Language Server Protocol operations and semantic code intelligence:
1. **Facade Abstraction**: Provides a stable, uniform C++ API for the rest of the editor (`project_manager`, `editor`, `codemap_utils`, tools) without exposing backend details.
2. **Strategy Delegation**: Owns a pointer to an `lsp_backend` strategy (`standard_lsp_backend` or `semcode_backend`), delegating all queries and document lifecycle notifications.
3. **Backend Lifecycle**: Manages startup, shutdown, backend switching, and fallback creation (`create_default_backend`).

## Architecture and Constraints
- **Ownership**: `lsp_manager` is owned by `project_manager`.
- **Strategy Pattern**: Decoupled from concrete language server processes or SQLite indexers via `lsp_backend`.
- **Non-Blocking Asynchronous Prefetching**: Forwards `prefetch_document_symbols` requests directly to the underlying backend to support parallel pre-warming of symbol caches.
- **Document Save Propagation**: Exposes `save_document` to synchronize full file saves (`didChange` + `didSave`) across language servers and initiate cache refreshes.

## Lessons Learned
- **Decoupled Facade**: Encapsulating backend strategy selection behind `lsp_manager` allows clean swapping between `standard_lsp_backend` and hybrid backends (`semcode_backend`) or test mocks without touching editor event handlers or tool implementations.
- **Prefetch Forwarding**: Providing non-blocking symbol prefetch in the facade allows high-level search tools like `fs_grep_files` to warm caches asynchronously without having to know or manage language server lifecycle details.
- **Preemptive Cache Synchronization**: Forwarding document save notifications through `lsp_manager` ensures underlying language servers are synchronized immediately after file writes, allowing high-level callers (`project_manager`) to invalidate caches and trigger symbol prefetching seamlessly.
