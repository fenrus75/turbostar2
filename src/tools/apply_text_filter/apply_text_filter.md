# Apply Text Filter Tool (`apply_text_filter_tool`)

## Goals
`apply_text_filter_tool` transforms text or file content using a registered filter in `filter_registry` (such as HTML-to-Markdown conversion) and either returns the filtered result or writes it to an output path.

## Constraints & Invariants
- Both `input_path` and `output_path` (when provided) must be validated via `ctx.fs_security.validate_access`.
- When writing to `output_path`, `project_manager::get_instance().notify_file_changed(args_.safe_output_path, result)` is invoked to invalidate all per-file caches and synchronize LSP state without duplicate `mapped_file::notify_changed` calls.

## Lessons Learned
- **Unified File-Change Fan-Out**: Previously, `apply_text_filter_tool` called `mapped_file::notify_changed` followed by `lsp_save_document` (which called `mapped_file::notify_changed` a second time). Delegating to `project_manager::notify_file_changed` eliminates the duplicate call while ensuring all caches (`mapped_file`, `codemap_cache`, `type_definition_cache`, `perf_manager`) are invalidated consistently.
