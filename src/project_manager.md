# Project Manager (`project_manager`)

## Goals
The `project_manager` class establishes and maintains the repository context and project boundaries for Turbostar. Its primary responsibilities include:
1. **Workspace Boundary Discovery**: Ascending directory walks to identify project root markers (`.git`, `meson.build`, `CMakeLists.txt`, `Cargo.toml`, `Makefile`).
2. **Project Rule Ingestion**: Reading and providing repository-specific guidelines from `GEMINI.md`, `AGENTS.md`, or `.cursorrules` to populate agent system prompts.
3. **LSP Lifecycle Coordination**: Creating, initializing, and binding `lsp_manager` and its active backend (`standard_lsp_backend` or `semcode_backend`) to the discovered root.
4. **Project Inventory & Layout**: Indexing file distributions, language breakdowns, and directory layouts asynchronously to provide quick context to LLM planning tools.
5. **Test Discovery & Compile Commands**: Discovering unit and end-to-end test suites across Meson (`meson test --list`) and CMake (`ctest -N`) and locating `compile_commands.json`.

## Architecture and Constraints
- **Singleton Lifecycle**: Accessed via `project_manager::get_instance()`. Must be initialized via `initialize()` before filesystem operations query `get_project_root()`.
- **Concurrency & Synchronization**:
  - Test list caches, project layout markdown, and project instructions are protected by `std::shared_mutex mutex_`.
  - The background inventory scanner runs in an interruptible worker thread (`inventory_thread_`), ensuring editor startup is never blocked by large codebases.
- **Teardown Contract**: Calling `shutdown()` triggers `set_exiting()`, interrupts the worker loop via condition variables, joins background threads, and stops LSP subprocesses cleanly before destroying database handles.

## Lessons Learned
- **Enforcing Initialization**: Uninitialized calls to `get_project_root()` can silently fall back to `.` or host working directories, causing test suites or tools to access unexpected locations. Adding `enforce_initialization_` with assertion checks prevents silent path leaks.
- **Test Discovery Mtime Caching**: Running `ctest -N` or `meson test --list` on every test tool execution introduces hundreds of milliseconds of latency. Tracking build definition mtimes (`build.ninja`, `CTestTestfile.cmake`, `CMakeCache.txt`) allows instant cache hits while guaranteeing invalidation when tests are added or modified.
- **Directory Isolation**: External project paths passed via CLI flags (`--project-dir`) must take precedence over heuristic git discovery, ensuring sandboxed unit test suites remain strictly isolated from the parent repository.
