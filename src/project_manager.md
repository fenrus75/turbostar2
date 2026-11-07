# Project Manager (`project_manager`)

## Goals
The `project_manager` class establishes and maintains the repository context and project boundaries for Turbostar. Its primary responsibilities include:
1. **Workspace Boundary Discovery**: Ascending directory walks to identify project root markers (`.git`, `meson.build`, `CMakeLists.txt`, `Cargo.toml`, `Makefile`).
2. **Project Rule Ingestion**: Reading and providing repository-specific guidelines from `GEMINI.md`, `AGENTS.md`, or `.cursorrules` to populate agent system prompts.
3. **LSP Lifecycle Coordination**: Creating, initializing, and binding `lsp_manager` and its active backend (`standard_lsp_backend` or `semcode_backend`) to the discovered root.
4. **Test Discovery & Compile Commands**: Discovering unit and end-to-end test suites across Meson (`meson test --list`) and CMake (`ctest -N`) and locating `compile_commands.json`.
5. **Unified File Change Fan-Out & Preemptive Cache Synchronization**: Dispatches `notify_file_changed(filepath, new_content)` (and `lsp_save_document`), invalidating all per-file caches (`mapped_file`, `codemap_cache`, `type_definition_cache`, `perf_manager`), synchronizing `didChange` and `didSave` across language servers, and triggering preemptive asynchronous codemap prefetching.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed via `project_manager::get_instance()`. Must be initialized via `initialize()` before filesystem operations query `get_project_root()`.
- **Concurrency & Synchronization**:
  - Test list caches and project instructions are protected by `std::shared_mutex mutex_`.
- **Teardown Contract**: Calling `shutdown()` triggers `set_exiting()`, stops background LSP subprocesses cleanly, and coordinates teardown before destroying database handles.

## Lessons Learned
- **Inventory Thread Deprecation**: The background inventory scanner thread that crawled the workspace tree on startup to construct directory layout markdown tables was deprecated and removed. Removing this background walk eliminated unnecessary disk I/O, thread overhead, and CPU cycles on editor startup.
- **Enforcing Initialization**: Uninitialized calls to `get_project_root()` can silently fall back to `.` or host working directories, causing test suites or tools to access unexpected locations. Adding `enforce_initialization_` with assertion checks prevents silent path leaks.
- **Test Discovery Mtime Caching**: Running `ctest -N` or `meson test --list` on every test tool execution introduces hundreds of milliseconds of latency. Tracking build definition mtimes (`build.ninja`, `CTestTestfile.cmake`, `CMakeCache.txt`) allows instant cache hits while guaranteeing invalidation when tests are added or modified.
- **Directory Isolation**: External project paths passed via CLI flags (`--project-dir`) must take precedence over heuristic git discovery, ensuring sandboxed unit test suites remain strictly isolated from the parent repository.
- **Preemptive Cache Synchronization on Edits**: Triggering `lsp_save_document` upon file modifications immediately notifies the language server via `didSave` and kicks off `prefetch_document_codemap_symbols`, ensuring that subsequent read/grep tool calls see freshly updated AST and codemap symbols without paying synchronous LSP reparse delays.
- **Unified File-Change Fan-Out (`notify_file_changed`)**: Previously, file-modifying tools (`fs_write_file`, `fs_replace_content`, `fs_replace_lines`, `apply_text_filter`, `git_restore`, `fs_man`, `web_fetch`) each called subsets of `mapped_file::notify_changed`, `codemap_cache::invalidate`, `type_definition_cache::invalidate_file`, `perf_manager::invalidate_file`, and `lsp_save_document`, leading to both redundant duplicate calls (e.g. calling `mapped_file::notify_changed` twice) and forgotten consumers (e.g. `perf_manager::invalidate_file` having zero callers, or `fs_man`/`web_fetch` omitting cache invalidation). Centralizing all per-file invalidation and LSP synchronization into `project_manager::notify_file_changed(filepath, new_content)` guarantees that every on-disk modification consistently updates all 6 consumers from a single entry point.
