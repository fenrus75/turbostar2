# Web Fetch Tool (`web_fetch_tool`)

## Goals
`web_fetch_tool` fetches remote HTTP/HTTPS resources with SSRF protection (blocking loopback/private IP ranges unless permitted), applies optional text filters, and either returns the content or writes it to `output_path`.

## Constraints & Invariants
- Enforces SSRF validation and maximum response size bounds (`20 MB`).
- Validates `output_path` write permissions via `file_security_manager` and notifies `project_manager::get_instance().notify_file_changed` when writing to a file.

## Lessons Learned
- **Cache Invalidation on Downloaded Output Files**: Calling `project_manager::notify_file_changed` after saving downloaded content to `safe_output_path` ensures that any previously cached `mapped_file` or `codemap_cache` state for that path is immediately invalidated.
