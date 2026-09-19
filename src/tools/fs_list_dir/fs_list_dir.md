# Directory Listing Tool (`fs_list_dir`)

## Goals
The `fs_list_dir` tool inspects and formats the contents of physical workspace directories and virtual file system (VFS) namespaces as structured Markdown tables. It displays filename, file type (`F` for file, `D` for directory, `L` for symlink), size in bytes, size in lines, and POSIX permissions, with optional rich libmagic MIME inspection.

## Architecture and Constraints
- **Pure Tool**: Marked as `is_pure() == true`, allowing safe invocation by read-only subagents.
- **Default Path Fallback**: The `path` parameter defaults to `.` (the project root), enabling seamless exploratory browsing without requiring callers to pass an explicit path argument. Also supports parameter aliases (`directory`, `dir`, `target_dir`).
- **Resilient Scanning**:
  - `scan_local_disk`: Wraps per-entry disk queries in non-throwing `std::filesystem::directory_iterator` calls so broken symlinks or unreadable entries do not abort the entire directory listing.
  - `scan_vfs`: Routes virtual URIs (e.g. `system://`, `tmp://`) to `virtual_file_system::list_directory`.
- **Pagination**: Supports `limit` (default: 100, max: 1000) and `offset` (default: 0) to avoid overwhelming model context windows when browsing large repositories.

## Lessons Learned
- **Defaulting to Project Root**: Requiring an explicit `path` argument caused unnecessary tool invocation errors when agents or users merely wanted to inspect the project root. Defaulting `path` to `.` aligns with standard shell `ls` semantics.
- **Symlink Target Traps**: Always use `symlink_status()` rather than `status()` to inspect directory entries to prevent following broken symlinks or hanging on special devices.
- **Project-Relative Wildcard Resteering**: If a caller passes a wildcard pattern (e.g. `*.cpp`) to `fs_list_dir`, returning an error that points callers to `fs_find_files(pattern=...)` with project-relative paths guides the agent to the appropriate tool.
