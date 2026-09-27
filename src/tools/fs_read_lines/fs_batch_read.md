#fs_batch_read

The `fs_batch_read` tool provides high-throughput, multi-chunk, and multi-file code reading capabilities within the TurboStar agentic framework.

## Goals

1. **Context Window Efficiency**: Allow agents to read multiple files or chunks in a single turn without saturating the context window with repetitive codemaps, called dependencies, and companion headers.
2. **Multi-Modal Code Reading**: Support three complementary reading types within a single request:
   - `lines`: Read line slices (or tail) from disk, active editor document snapshot, or VFS.
   - `symbol`: Query LSP document symbols and retrieve the exact definition slice for a given symbol name.
   - `class_context`: Extract class context preview (referenced member variables and methods) for C++ implementation files.
3. **Resilience & Partial Failure**: When reading a batch of items, failures in individual items (e.g. unknown symbol or file not found) output inline error notes rather than aborting the entire batch.
4. **Zero Overhead for Single-Item Queries**: When invoked with a batch size of 1 for line reading, automatically delegates to `fs_read_lines` to retain the rich interactive codemap overview and candidate type harvesting.

## Constraints & Security

- **Strict Sandbox Permissions**: All file paths undergo Stage 1 validation via `file_security_manager::validate_access` with `access_type::read`.
- **Bounded Batch Size**: Batches are capped at 50 items to prevent runaway resource consumption.
- **Pure Tool**: Registered as pure (`is_pure() == true`) with no side-effects on workspace state.

## Lessons Learned

- **Boundary Heuristics**: Reusing `read_file_lines` and `format_file_lines_markdown` across both `fs_read_lines` and `fs_batch_read` eliminates code duplication while ensuring consistent syntax formatting, line numbering, and boundary clamping.
- **Graceful Error Inlining**: Storing item-level errors (`has_error`, `error_message`) inside `batch_read_item` enables the validator to accept batches where one file path might be missing or out-of-bounds, surfacing clear per-item diagnostic errors in the final output.
