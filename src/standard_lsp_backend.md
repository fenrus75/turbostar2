# Standard LSP Backend (`standard_lsp_backend`)

## Goals
The `standard_lsp_backend` class (`src/standard_lsp_backend.h`, `src/standard_lsp_backend.cpp`) manages the lifecycle and JSON-RPC communication for external Language Server Protocol processes:
1. **Server Orchestration**: Spawns and supervises language servers (such as `clangd` for C/C++ and `pylsp` for Python) over standard I/O pipes.
2. **Document Synchronization**: Coordinates `textDocument/didOpen`, `textDocument/didChange`, and `textDocument/didSave` events to keep language servers synchronized with editor buffers and disk edits.
3. **Language Queries**: Provides definitions, type definitions, references, hover tips, selection ranges, document/workspace symbols, and call hierarchies.
4. **Diagnostic Management**: Collects compiler errors and linter warnings from language servers and reports them to the editor UI.

## Architecture and Constraints
- **Multi-Server Concurrency**: Maintains separate server instances per language family (`cpp`, `python`) guarded by `servers_mutex_`.
- **Extension Normalization**: File extension matching uses `utf8::to_lower` to ensure reliable language dispatch independent of case sensitivity or locale quirks.
- **Cache Management**: Caches hover data, symbol nodes, and selection ranges with thread-safe invalidation upon buffer edits and document saves.
- **Document State Protocol Adherence**: Calling `update_document` or `save_document` on previously unopened files automatically dispatches `textDocument/didOpen` first, ensuring compliant version tracking (`doc_versions_`) and preventing LSP server drops or protocol errors.

## Lessons Learned
- **Extension Normalization**: Normalizing file extensions with `utf8::to_lower` avoids locale-dependent bugs and signed-char undefined behavior when routing documents to `clangd` or `pylsp`.
- **Asynchronous Document Symbol Prefetching**: Sequential synchronous calls to `query_document_symbols` across multiple unindexed candidate files (e.g. during search annotations) accumulate multi-second timeouts. Decoupling prefetch requests into non-blocking JSON-RPC calls with `in_flight_symbol_queries_` deduplication allows the language server to process files concurrently and warm `symbol_cache_` ahead of time.
- **Edit and Save Synchronization**: Emitting `textDocument/didSave` alongside `textDocument/didChange` upon any file modification signals language servers to flush compiler state and diagnostics, while immediately invalidating stale symbol caches and triggering preemptive background codemap caching.
- **Asynchronous Document Synchronization & Non-Blocking Startup**: Launching and initializing language servers (`clangd`) involves process creation, pipes, and JSON-RPC initialization handshakes. Running `open_document`, `update_document`, and `save_document` via an asynchronous FIFO background worker (`doc_worker_thread_`) prevents language server startup from blocking the editor UI thread or freezing the event loop during application startup. Furthermore, passive queries such as `query_file_diagnostics`, `request_hover`, and highlights only query running servers (`start_if_needed = false`), ensuring the main thread never stalls waiting on server initialization.
