# Virtual File System (`virtual_file_system`)

## Goals
The `virtual_file_system` (VFS) class abstracts file and memory storage across custom URI schemes (`system://`, `tmp://`, `skills://`, `github://`), providing unified read, write, and directory inspection interfaces for agents and the editor.

## Architecture and Constraints
- **Zero-Copy Buffers**:
  - Encapsulates content access behind `vfs_content_buffer` and `vfs_file_handle`, supporting both in-memory strings (`string_content_buffer`) and zero-copy memory maps (`mmap_content_buffer`).
- **Modular VFS Providers**:
  - `memory_vfs_provider`: Manages ephemeral in-memory files (e.g. `tmp://` scratchpad files, generated artifacts).
  - `system_vfs_provider`: Exposes internal system documentation, project status reports, and active test lists under `system://`.
  - Dynamic providers: Mounts skill repositories and remote git trees on demand.
- **Thread Safety**:
  - Provider lookups, directory listings, and mounts are guarded by `std::mutex mutex_`.

## Lessons Learned
- **Preventing Physical Disk Pollution**: Providing `tmp://` and in-memory VFS providers prevents autonomous subagents from littering scratch files across the host user's project workspace.
- **Zero-Copy Memory Mapping**: Using `mmap_content_buffer` for large virtual files and binary containers prevents high heap churn when inspecting multi-megabyte files.
