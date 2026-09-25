# Codemap Utilities (`codemap_utils`)

## Goals
`codemap_utils` provides extraction, hierarchical structuring, formatting, and analysis of code symbols (classes, functions, methods, enums, structs, interfaces) across source code and markdown documents in Turbostar. It serves tools like `fs_file_codemap`, `fs_read_lines`, `fs_replace_content`, `fs_grep_files`, and crash analysis.

## Architecture and Constraints
- **LSP and Fallback Extraction**:
  - Leverages LSP document symbol requests via `project_manager::lsp_query_document_symbols` when a language server is available.
  - Implements lightweight AST/regex heuristic fallback parsing (`fallback_find_symbols`) for C/C++, Python, and SystemVerilog when language servers are unavailable, cold, or uninstalled.
  - Implements specialized Markdown header parsing (`parse_markdown_headings`).
- **Hierarchy Structuring**:
  - Reconstructs nested symbol relationships (`structure_symbol_hierarchy`) based on container prefixes, namespaces, and line ranges.
- **Symbol Kinds**:
  - Aligns strictly with the LSP `SymbolKind` standard (1: File, 5: Class, 6: Method, 9: Constructor, 10: Enum, 11: Interface, 12: Function, 13: Variable, 23: Struct, 26: TypeParameter).

## Lessons Learned
- **LSP SymbolKind Enum Alignment**: Standard LSP specification defines 10 for Enum, 11 for Interface, 12 for Function, and 13 for Variable. Off-by-one errors in symbol kind mapping tables caused LSP-extracted functions (kind 12) to be misclassified as "Variable" instead of "Function", breaking downstream scope balance checks and symbol queries.
- **Brace Balance Warnings & Scope Detection**: When files contain pre-existing unbalanced braces at file scope (e.g. malformed or test files), downstream edits check symbol scopes for introduced imbalances. Ensuring function symbols are correctly categorized preserves precise scope warnings.
