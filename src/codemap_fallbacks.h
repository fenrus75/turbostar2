#pragma once

#include <span>
#include <string>
#include <vector>
#include "codemap_utils.h"

namespace tools
{

/**
 * @brief Checks if the given file path has a Markdown file extension (.md, .markdown, .mdown, .mkd).
 * @param filepath File path or URI to inspect.
 * @return true if filepath is a Markdown document, false otherwise.
 */
[[nodiscard]] bool is_markdown_file(std::string_view filepath) noexcept;

/**
 * @brief Parses Markdown headings into a hierarchical codemap symbol list.
 *
 * Markdown has no external language server requirement. Lines starting with one or more '#'
 * characters are extracted as outline symbols with their heading level mapped to outline depth.
 *
 * @param content Full text content of the Markdown document.
 * @param min_lines Minimum span of lines for a section to be included.
 * @param out Output symbol vector to append parsed headings into.
 */
void parse_markdown_headings(const std::string &content, int min_lines, std::vector<codemap_symbol_info> &out);

/**
 * @brief Fallback symbol parser for Markdown document lines.
 *
 * Scans lines for ATX headings (# through ######) and maps heading levels to outline depth.
 *
 * @param lines Read-only sequence of document lines.
 * @param min_lines Minimum span of lines for a section to be included (default 1).
 * @param out Output symbol vector to append parsed headings into.
 */
void fallback_find_symbols_markdown(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out);
void fallback_find_symbols_markdown(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out);

/**
 * @brief Fallback symbol parser for C and C++ source code lines.
 *
 * Scans lines using RE2 patterns, curly-brace stack tracking, and specifier keyword
 * inspection to detect functions, classes, structs, and enums while skipping prototypes.
 *
 * @param lines Read-only sequence of document lines.
 * @param min_lines Minimum span of lines for a symbol to be included (default 1).
 * @param out Output symbol vector to append detected symbols into.
 */
void fallback_find_symbols_cpp(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out);
void fallback_find_symbols_cpp(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out);

/**
 * @brief Fallback symbol parser for Python source code lines.
 *
 * Scans lines for `def` function and `class` definitions.
 *
 * @param lines Read-only sequence of document lines.
 * @param min_lines Minimum span of lines for a symbol to be included (default 1).
 * @param out Output symbol vector to append detected symbols into.
 */
void fallback_find_symbols_python(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out);
void fallback_find_symbols_python(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out);

/**
 * @brief Fallback symbol parser for Verilog and SystemVerilog source code lines.
 *
 * Scans lines for modules, interfaces, packages, classes, functions, and tasks,
 * pairing each with its corresponding `end*` keyword.
 *
 * @param lines Read-only sequence of document lines.
 * @param min_lines Minimum span of lines for a symbol to be included (default 1).
 * @param out Output symbol vector to append detected symbols into.
 */
void fallback_find_symbols_verilog(std::span<const std::string> lines, int min_lines, std::vector<codemap_symbol_info> &out);
void fallback_find_symbols_verilog(std::span<const std::string> lines, std::vector<codemap_symbol_info> &out);

/**
 * @brief Top-level router for regex/heuristic fallback symbol parsing from a disk file.
 *
 * Opens the file, reads lines, identifies language family by extension, and delegates
 * to the appropriate language-specific fallback parser.
 *
 * @param safe_path Cleaned absolute or canonical path to the source file.
 * @param min_lines Minimum line length for a symbol to be included in the output.
 * @param out Output symbol vector to append detected symbols into.
 */
void fallback_find_symbols(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out);

} // namespace tools
