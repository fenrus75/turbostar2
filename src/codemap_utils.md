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
- **Regex `sub_match::str()` Lifetime and `std::string_view`**: Calling `match.suffix().str()` creates a temporary `std::string` that is destroyed at the end of the full-expression. Binding this to `std::string_view` leaves a dangling view pointing to deallocated memory, leading to use-after-free and crash risks when inspected. Instead, construct `std::string_view(match.suffix().first, match.suffix().second)` directly over the underlying iterator range to avoid heap allocations and eliminate dangling references.
- **Class Context Preview Method Scoping**: When `extract_class_context_preview` resolves `enclosing` to an outer Class/Struct (e.g. when reading ranges starting on blank lines or partial functions), `simple_class_name` is known but `method_name` is empty. Scanning `read_lines` with `class_method_re` to resolve `method_name` ensures the method currently being defined or read is excluded from the list of referenced sibling methods.
- **Thread Lifecycle & RE2 Migration**: Background refresh threads (`refresh_outgoing_calls_async`) must not be detached without tracking; tearing down the process while background threads parse large files (e.g. 10k+ lines of header files) leads to race conditions against global static destructors and libc exit handlers. Background workers must be tracked and joined during `project_manager::shutdown()`, and parsing loops must check `project_manager::is_exiting()` periodically. In addition, migrating `fallback_find_symbols` from `std::regex` to `re2::RE2` eliminates libstdc++ regex NFA backtracking recursion bugs and assertion failures.

