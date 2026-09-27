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
