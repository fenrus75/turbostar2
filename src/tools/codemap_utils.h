#pragma once

#include "agentlib/tool_context.h"
#include "lsp_manager.h"
#include <string>
#include <vector>

namespace tools {

struct codemap_symbol_info {
	std::string name;
	std::string display_name; // Option B indented name (e.g. "    ::get_instance")
	std::string kind_str;
	int start_line;
	int end_line;
	int line_count;
	int depth{0};
};

struct codemap_selection_result {
	std::vector<codemap_symbol_info> selected_symbols;
	size_t total_symbols{0};
	size_t omitted_count{0};
};

// Query LSP (or regex fallback) and collect flat symbol list for a given file
std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx, int min_lines = 1);

// Select and prioritize top N codemap symbols for a file read range based on scope, search history, position, and deduplication
codemap_selection_result select_prioritized_codemap_symbols(
	const std::vector<codemap_symbol_info> &all_symbols,
	int read_start,
	int read_end,
	const std::string &safe_path,
	agentlib::tool_context &ctx,
	size_t max_items = 10);

// Format codemap symbols into Markdown table
// If rich_format is true: 5 columns (| Symbol | Kind | Start Line | End Line | Lines |)
// If rich_format is false: 3 columns (| Function | Start Line | End Line |)
std::string format_codemap_table(
	const std::string &display_path,
	const std::vector<codemap_symbol_info> &symbols,
	bool rich_format,
	size_t total_file_lines = 0,
	size_t total_symbols_count = 0,
	size_t omitted_count = 0,
	agentlib::tool_context *ctx = nullptr);

// Find matching implementation file for a header file (e.g. .h -> .cpp, .hpp -> .cpp)
std::string find_matching_impl_file(const std::string &header_path, agentlib::tool_context &ctx);

// Find the innermost symbol enclosing line_number in a list of codemap symbols
const codemap_symbol_info* find_enclosing_symbol(const std::vector<codemap_symbol_info> &symbols, int line_number);

} // namespace tools
