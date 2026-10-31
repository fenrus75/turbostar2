# `fs_regexp_lines` Tool

## Goals
The `fs_regexp_lines` tool filters lines from a single document (active editor buffer, VFS stream, or disk file) matching a regular expression pattern:
1. **Targeted Line Extraction**: Surfaces matching lines with their 1-based line numbers in Markdown table format without reading or loading the entire document into LLM context.
2. **Multi-Source Support**: Intercepts active editor document buffers via `doc_provider`, VFS URI paths (`include://`, `system://`), and local disk files.
3. **Safety & Bounds**: Enforces maximum match limits (default 1000 lines) and binary file checks to prevent context overflow or terminal corruption.

## Architecture and Constraints
- **Two-Stage Validation**: `validate_runtime` pre-compiles the regular expression and validates syntax before execution.
- **Markdown Table Output**: Results are formatted as `| Line Number | Content |` tables, escaping pipe characters as `&#124;` and stripping ANSI escape sequences.
- **PCRE2 Execution**: Utilizes `pcre2_regex` for fast pattern evaluation with Unicode support and JIT acceleration.

## Lessons Learned
- **PCRE2 Migration**: Migrated regex matching from `re2::RE2` to `pcre2_regex`, removing transitive Abseil dependencies while maintaining full Unicode regex support and safety limits against ReDoS.
