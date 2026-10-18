# Codemap Fallbacks (`codemap_fallbacks`)

## Goals
The `codemap_fallbacks` module (`src/codemap_fallbacks.h`, `src/codemap_fallbacks.cpp`) provides fast, lightweight heuristic and regex-based symbol extraction when external Language Server Protocol (LSP) servers are unavailable, unindexed, or unsupported:
1. **Zero-Dependency Symbol Extraction**: Extracts classes, structs, enums, functions, modules, tasks, and interfaces without requiring a running LSP daemon, `compile_commands.json`, or external subprocesses.
2. **Multi-Language Coverage**: Provides regex parsers for C, C++, Python, and SystemVerilog/Verilog.
3. **Markdown Outline Extraction**: Treats Markdown headings (`#` through `######`) as structured outline symbols, giving agents instant outline context on documentation and notes.

## Architecture and Constraints
- **Sub-Millisecond Execution**: Operates purely in-memory using RE2 patterns, string scanning, and brace balance counters, achieving <1ms execution times per file.
- **Language-Specific In-Memory Slices**: Refactored into discrete language helpers (`fallback_find_symbols_cpp`, `fallback_find_symbols_python`, `fallback_find_symbols_verilog`, `fallback_find_symbols_markdown`) operating on `std::span<const std::string>`, allowing isolated unit testing without requiring real disk files. Top-level router `fallback_find_symbols` unifies file I/O and routes to all four helpers based on extension.
- **Interruption Support**: Checks `project_manager::get_instance().is_exiting()` periodically (every 64 lines) during long file scans to avoid delaying application teardown.
- **Independence from LSP**: Completely decoupled from LSP data types (`symbol_node`), JSON-RPC message framing, or process lifecycles. Emits clean `codemap_symbol_info` structures directly usable by `codemap_cache` and `codemap_utils`.

## Lessons Learned
- **Separation of Concerns**: Splitting regex-based syntactic heuristics out of `codemap_utils.cpp` keeps high-level codemap formatting and caching logic uncluttered while giving non-LSP fallback parsing a dedicated, easily testable home.
- **Decoupled Language Parsers**: Python (indentation/def/class), SystemVerilog (module/endmodule pair matching), and C/C++ (brace tracking and specifier allowlists) have zero algorithmic overlap. Modularizing them into dedicated functions operating on `std::span<const std::string>` eliminates redundant per-line branching and allows independent evolution and testing of grammar heuristics.
- **Brace & Scope Matching**: C++ functions often have multi-line attribute prefixes, return types, and templates (`[[nodiscard]] inline constexpr auto`). Searching backward for specifier keywords and matching forward balanced curly braces avoids mistaking forward declarations or macro invocations for full definitions.
