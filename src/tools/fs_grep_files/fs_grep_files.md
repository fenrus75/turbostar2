# `fs_grep_files` Tool

## Goals
`fs_grep_files` provides pattern and regular-expression code search across project directories and virtual file system (VFS) documents for LLM agents. Rather than dumping raw text matches, it enriches results with contextual line ranges, surrounding AST/LSP symbol hints (enclosing function, class, or method scope), and duplicate query protection.

## Architecture and Constraints
- **Two-Stage Validation Pipeline**:
  - `fs_grep_files_validator`: Validates and canonicalizes search paths using `file_security_manager`, binds input aliases (`path` for `search_path`, `query` for `pattern`), clamps `limit` and `context_lines`, and enforces purity (`is_pure() == true`).
  - `fs_grep_files_tool`: Executes the search across editor buffers, local disk paths, or VFS mounts.
- **Search Scope & Priority Tiers**:
  - Prioritizes matches across four tiers:
    - Tier 1: Active editor buffers and primary code files (`.cpp`, `.h`, `.py`, `.rs`, `.go`, etc.).
    - Tier 2: General project documents and configs.
    - Tier 3: Build artifacts, vendor headers, and logs.
    - Tier 4: Editor temporary / backup files (`*~`, `*.swp`, `*.bak`).
- **Binary Filtering**:
  - Files are inspected with `fs_utils::is_binary_file(path, /*treat_null_as_binary=*/true)` for disk files and `fs_utils::is_binary_buffer(content, /*treat_null_as_binary=*/true)` for VFS streams.
  - By default (`include_binary = false`), binary files (containing control characters or null bytes) are excluded from the search.
  - When `include_binary = true`, binary files are included in the search pass.
- **Duplicate Query Guard**:
  - Detects identical sequential queries (`g_last_search`) and alerts the agent with actionable guidance instead of redundantly flooding the context window.

## Lessons Learned
- **Null-Byte Binary Exclusion**: Tar archives, ELF object files, and compiled assets frequently contain null bytes without other ASCII control characters in their initial blocks. Grepping these as text results in garbled matches or token pollution. Explicitly treating null bytes as binary (`treat_null_as_binary=true`) ensures clean text search by default, while the `include_binary` parameter preserves the ability to locate strings inside binary files when intentionally desired.
- **Markdown Escaping in Code Previews**: Line text emitted in bullet points (`* **Line N**: ...`) contains markdown metacharacters (e.g. `_`, `*`, `\``) that must be escaped to prevent UI rendering breakages and prompt context confusion.
