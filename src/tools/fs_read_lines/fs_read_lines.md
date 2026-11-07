#fs_read_lines

## Overall Goals and Architecture
`fs_read_lines` is the primary file-reading tool for agents in TurboStar. It provides line-numbered slices of code files, open editor buffers, or Virtual File System (VFS) resources.
Key features:
1. **Multi-Source Line Resolution**: Transparently resolves line slices from active editor document buffers (reflecting unsaved modifications), Virtual File System providers (`system://`, `github://`, `tmp://`), or regular disk files.
2. **Semantic Boundary Snapping**: Gently expands requested line slices to avoid cutting off functions or blocks in the middle when reading code.
3. **Integrated Codemap Summaries**: Inlines a compact symbol codemap, referenced class member context, called cross-file dependencies, and referenced type definitions to provide complete orientation in a single turn.

## Constraints
- Validates read access through `tool_context::fs_security` to enforce workspace boundaries.
- Rejects binary files to protect context windows and prevent raw binary dumps.
- Hard limits maximum slice lengths to prevent accidental context overflow.

## Lessons Learned
- **Cross-File Codemap Dependencies**: When resolving called dependencies, always prioritize actual `.cpp` implementation files over `.h` declaration signatures so agents jump directly to executable code.
- **Enclosing Self-Definitions**: When scanning slice lines for function calls, definition headers matching the current function must be filtered out so the function is not reported as calling its own declaration.
- **Zero-Copy `std::string_view` Slices & Single-Buffer Formatting**: Constructing a `std::vector<std::string>` by copying each line byte-by-byte, re-concatenating those lines into temporary `std::string` buffers for `type_token_extractor` and `extract_class_context_preview`, and formatting line numbers via per-line `std::format` + `std::stringstream` accounted for over 85% of warm `fs_read_lines` execution time (~9.0 ms avg). Holding `mapped_file` / VFS backing handles in `file_read_result` with `std::vector<std::string_view> lines` and a contiguous `raw_range_view`, skipping control-character scanning inside the first 4,096 bytes already verified clean by `mapped_file::is_binary()`, and formatting into a single pre-reserved `std::string` using stack `std::to_chars` reduced warm average latency to ~0.76 ms (an 11.9x speedup).
