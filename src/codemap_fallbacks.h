#pragma once

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
 * @brief Regex/heuristic fallback symbol parser for source code (C, C++, Python, SystemVerilog).
 *
 * Invoked when Language Server Protocol (LSP) backends are unavailable, unindexed, timed out,
 * or return an empty symbol list. Scans source text using RE2 patterns and curly-brace / indentation
 * tracking to detect functions, classes, structs, enums, interfaces, modules, and tasks.
 *
 * @param safe_path Cleaned absolute or canonical path to the source file.
 * @param min_lines Minimum line length for a symbol to be included in the output.
 * @param out Output symbol vector to append detected symbols into.
 */
void fallback_find_symbols(const std::string &safe_path, int min_lines, std::vector<codemap_symbol_info> &out);

} // namespace tools
