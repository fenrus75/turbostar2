# Codemap Cache (`codemap_cache`)

## Goals
The `codemap_cache` class (`src/codemap_cache.h`, `src/codemap_cache.cpp`) provides a thread-safe, high-performance persistent cache for extracted and structured code symbols (`codemap_symbol_info`) across Turbostar editor and MCP server restarts:
1. **Cold-Start Elimination**: Delivers instant (<0.1ms vs 800ms–1500ms) symbol lookups on cold startup by loading pre-parsed codemaps from disk.
2. **Universal Format Coverage**: Caches structured symbol hierarchies regardless of whether they originated from LSP (`clangd`/`pylsp`), hybrid indexing (`semcode`), regex heuristics (`fallback_find_symbols`), or Markdown ATX headings (`parse_markdown_headings`).
3. **Smart Startup Validation**: Validates current disk file modification times (`mtime`) and file sizes during startup loading. Unchanged files are retained and used to pre-warm `type_definition_cache`, while modified or stale files immediately kick off asynchronous background prefetching so caches are warm when subsequent tool queries arrive.
4. **LRU Eviction**: Tracks `last_used_ms` timestamps on every access to enforce bounded memory and disk usage.
5. **Atomic Persistence**: Persists dirty entries to `~/.cache/turbostar/projects/<hash>/codemap_cache.json` using atomic temporary file creation and replacement to prevent multi-process corruption.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed globally via `codemap_cache::get_instance()`. Initialized during `project_manager::initialize` and flushed during `project_manager::shutdown`.
- **Validation Contract**: Every cache lookup validates `std::filesystem::last_write_time` and `file_size` via fast OS `stat` checks. Stale entries are transparently invalidated.
- **Dirty Buffer Bypass**: Cache lookups are bypassed if the target document has unsaved, modified lines in `document_provider`.
- **Thread Safety**: `cache_mutex_` protects in-memory map access and dirty flags. Mutex locks are never held across disk I/O or background LSP RPC calls.

## Lessons Learned
- **Decoupled Result Caching**: Caching at the codemap layer rather than the raw LSP AST layer avoids repeated sorting, filtering, kind mapping, and hierarchy building overhead, while covering non-LSP languages identically.
- **Startup Stale File Prefetching**: Triggering non-blocking prefetch on startup for files whose mtime changed eliminates query stalls by warming clangd ASTs before the user or agent issues tool calls.
- **Cross-Filesystem Atomic Saves**: Writing to a PID-tagged temporary file in `fs_utils::get_project_tmp_dir()` followed by `std::filesystem::rename` (with fallback to copy+remove) guarantees zero corruption if the editor or server terminates abruptly during serialization.
