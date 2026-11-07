# Man Page Tool (`fs_man_tool`)

## Goals
`fs_man_tool` locates system or project manual pages, decompresses and converts troff/mandoc formatting into clean Markdown (`troff2md`), and optionally writes the output to a file (`output_path`).

## Constraints & Invariants
- Validates `output_path` against `file_security_manager` write permissions before writing.
- Notifies `project_manager::get_instance().notify_file_changed` when `output_path` is written so cached file views and Markdown heading codemaps are invalidated.

## Lessons Learned
- **Cache Invalidation on Output File Writes**: Writing converted Markdown to `output_path` without notifying `project_manager::notify_file_changed` could leave stale `mapped_file` or `codemap_cache` entries if the target file had been read previously.
