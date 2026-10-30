# Codemap Cache (`codemap_cache`)

## Goals
The `codemap_cache` class (`src/codemap_cache.h`, `src/codemap_cache.cpp`) provides a thread-safe, high-performance persistent cache for extracted and structured code symbols (`codemap_symbol_info`) across Turbostar editor and MCP server restarts:
1. **Cold-Start Elimination Without Startup Cost**: Delivers instant (<0.1ms vs 800ms–1500ms) symbol lookups by persisting pre-parsed codemaps to disk, while completely deferring disk loading until first use so application startup latency remains zero.
2. **Universal Format Coverage**: Caches structured symbol hierarchies regardless of whether they originated from LSP (`clangd`/`pylsp`), hybrid indexing (`semcode`), regex heuristics (`fallback_find_symbols`), or Markdown ATX headings (`parse_markdown_headings`).
3. **Lazy Startup & Smart Validation**: `initialize()` merely records the project root without performing disk I/O or JSON parsing. On first query, persisted entries are loaded lazily. Disk file modification times (`mtime`) and file sizes are validated via fast `stat()` checks: unchanged files are retained and used to pre-warm `type_definition_cache`, while modified or stale files kick off asynchronous background prefetching.
4. **LRU Eviction**: Tracks `last_used_ms` timestamps on every access to enforce bounded memory and disk usage.
5. **Atomic Persistence**: Persists dirty entries to `~/.cache/turbostar/projects/<hash>/codemap_cache.json` using atomic temporary file creation and replacement to prevent multi-process corruption.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed globally via `codemap_cache::get_instance()`. Initialized during `project_manager::initialize` and flushed during `project_manager::shutdown`.
- **Lazy Initialization**: `initialize` sets the project root; disk loading is deferred via `ensure_loaded()` until `get()`, `put()`, `has_valid_entry()`, or `size()` is called. In pure editor sessions that do not inspect symbols or execute agent codemap tools, disk reading and hundreds of `stat()` operations are completely avoided.
- **Safe Early Invalidation**: Calling `invalidate()` before disk data is loaded safely returns early without forcing a load, because subsequent loading validates mtimes and discards modified files.
- **Validation Contract**: Every cache lookup validates `std::filesystem::last_write_time` and `file_size` via fast OS `stat` checks. Stale entries are transparently invalidated.
- **Dirty Buffer Bypass**: Cache lookups are bypassed if the target document has unsaved, modified lines in `document_provider`.
- **Thread Safety**: `cache_mutex_` protects in-memory map access and dirty flags. `load_mutex_` serializes lazy loading on first access. Mutex locks are never held across disk I/O or background LSP RPC calls.

## Lessons Learned
- **Decoupled Result Caching**: Caching at the codemap layer rather than the raw LSP AST layer avoids repeated sorting, filtering, kind mapping, and hierarchy building overhead, while covering non-LSP languages identically.
- **Deferred Initialization for Pure Editor Performance**: Loading hundreds of cached symbol entries and calling `stat()` on each file during editor startup cost ~100ms and saturated I/O queues before the user even began typing. Deferring loading to first use eliminates startup overhead entirely while preserving fast warm access when the agent or symbol inspector is actually invoked.
- **Startup Stale File Prefetching**: Triggering non-blocking prefetch on load for files whose mtime changed eliminates query stalls by warming clangd ASTs before the user or agent issues tool calls.
- **Cross-Filesystem Atomic Saves**: Writing to a PID-tagged temporary file in `fs_utils::get_project_tmp_dir()` followed by `std::filesystem::rename` (with fallback to copy+remove) guarantees zero corruption if the editor or server terminates abruptly during serialization.
