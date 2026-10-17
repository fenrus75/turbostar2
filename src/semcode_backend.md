# Semcode Backend (`semcode_backend`)

## Goals
`semcode_backend` integrates the `semcode` code intelligence system into Turbostar as a specialized LSP and indexer backend. It combines the `semcode-lsp` language server over JSON-RPC stdio with high-performance local SQLite index queries (`semcode_indexer`) for fast cross-file symbol resolution, definition lookup, reference tracking, call hierarchy extraction, and semantic search.

## Architecture and Constraints
- **Subclassing Standard LSP**: Inherits from `standard_lsp_backend`, specializing communication for projects indexed with `.semcode.db`.
- **Working-Directory Overlay**: Unlike full language servers that require continuous full-document body syncing over stdio, `semcode` relies on working-directory file access. `open_document` and `update_document` are optimized accordingly.
- **Identifier Boundary Scanning (`extract_identifier_at`)**:
  - Scans backward and forward from cursor positions using `utf8::is_alnum(..., "_")` to extract C/C++ tokens safely without ctype undefined behavior on non-ASCII characters.
  - Skips leading C/C++ function qualifiers and keywords (e.g. `static`, `inline`, `virtual`, `constexpr`, `noexcept`, `asmlinkage`) when resolving symbols at the start of a definition line.
- **Thread Safety**: Protects background indexing tasks and synchronous query invocations with dedicated mutexes and condition variables.

## Lessons Learned
- **Ctype Undefined Behavior in Token Scanning**: Direct calls to `<cctype>` functions (`std::isalnum`) with `char` values $\ge 0x80$ trigger undefined behavior or assertion crashes in debug runtimes. Using `utf8::is_alnum(c, "_")` guarantees signedness safety and predictable ASCII identifier boundary detection.
- **Specifier Stripping for Declaration Lines**: When querying definitions on lines beginning with return types or linkage specifiers, extracting the first word matches the specifier rather than the function or variable name. Skipping known specifiers allows precise definition lookup when hovering over function declarations.
