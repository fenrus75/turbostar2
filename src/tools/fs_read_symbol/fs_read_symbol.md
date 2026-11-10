# fs_read_symbol_tool (`src/tools/fs_read_symbol/`)

## Goals & Constraints
- Reads the full definition and line range of a named symbol (function, method, class, struct, module, task, interface, or heading) from a source file without requiring the caller to guess line numbers.
- Uses `get_document_codemap_symbols` (`codemap_utils`) so symbol resolution automatically leverages LSP when available and transparently falls back to `codemap_fallbacks` (including SystemVerilog/Verilog `.sv`/`.v`, Python `.py`, Markdown `.md`, and C/C++ heuristic parsers) when no LSP server is configured for the file type.
- Supports scoped names (`Class::method`, `Class.method`) as well as Verilog module alias conventions (`<name>_mod` or filename stem matching the file's `Module` symbol).
- Purely read-only (`is_pure() == true`) and enforces sandbox path validation (`fs_security.validate_access`) and binary file rejection.

## Lessons Learned
- **Unifying Symbol Resolution via `codemap_utils`**: Previously, `fs_read_symbol_tool` gated execution on `project_manager::lsp_is_supported_file` and queried `lsp_query_document_symbols` directly, causing hard failures on languages without an external LSP server such as Verilog/SystemVerilog (`.sv`, `.v`). Routing `fs_read_symbol_tool` through `get_document_codemap_symbols` reuses the existing `codemap_cache` and `codemap_fallbacks` infrastructure (`fallback_find_symbols_verilog`, etc.) with zero duplication.
- **Verilog Filename Stem & `_mod` Fallback**: In RTL projects where a file `alu.sv` defines `module alu_mod`, callers frequently pass `symbol_name="alu"` matching the filename stem. Allowing a fallback match on `<name>_mod` or the filename stem for `Module` symbols in `.sv`/`.v` files avoids unnecessary round-trip failures.
