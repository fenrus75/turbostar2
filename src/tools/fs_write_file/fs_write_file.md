# `fs_write_file_tool` (`src/tools/fs_write_file/`)

## Overview & Goals
`fs_write_file_tool` creates, overwrites, or appends to files on disk or in the Virtual File System (`tmp://`, `clipboard://`) while enforcing workspace security boundaries, open-editor-buffer conflict checks, LSP synchronization, and file health tracking.

## Constraints & Invariants
1. **Active Editor Protection**: Rejects writes to files currently open in the interactive editor (`validate_runtime`) to prevent clobbering unsaved user buffers.
2. **Zero Redundant Disk Re-Reads**: When overwriting a file (`append == false`), `args_.content` is passed directly to `lsp_save_document` without reopening and re-reading the file from disk.
3. **Cache & LSP Synchronization Before Health Check**: `lsp_save_document` is invoked prior to `update_file_health_state` so `mapped_file`, `codemap_cache`, and LSP see the updated file state immediately.

## Lessons Learned
- **Redundant Byte-by-Byte Re-Read & Uncached `compile_commands.json` Lookup**:
  - Previously, `fs_write_file_tool::execute` reopened the freshly written file via `std::istreambuf_iterator<char>` even on non-append writes, and triggered an uncached full parse of `compile_commands.json` in `update_file_health_state`. Reusing `args_.content` directly and caching `compile_commands.json` in `fs_utils::get_compile_command_for_file` eliminated ~40–80 ms of overhead on non-C++ writes.
