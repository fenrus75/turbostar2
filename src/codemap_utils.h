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
	std::string source_file; // Source file path if cross-file dependency
};

struct codemap_selection_result {
	std::vector<codemap_symbol_info> selected_symbols;
	size_t total_symbols{0};
	size_t omitted_count{0};
};

struct outgoing_call_reference {
	std::string caller_file;       // Source file where call occurs
	int call_line{0};              // Line number of the call site
	std::string target_name;       // Name of called function/method
	std::string target_file;       // File path where target is defined (.cpp implementation preferred)
	int target_start_line{0};      // Start line of target implementation
	int target_end_line{0};        // End line of target implementation
	std::string target_kind;       // "Method", "Function", "Class", etc.
	bool is_direct_read_call{true};// True if called inside [read_start, read_end]
};

// Identifies functions called within [start_line, end_line] of safe_path.
std::vector<outgoing_call_reference> get_outgoing_calls_in_range(
	const std::string &safe_path,
	int start_line,
	int end_line,
	agentlib::tool_context *ctx = nullptr,
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

std::vector<outgoing_call_reference> get_outgoing_calls_in_range(
	const std::string &safe_path,
	int start_line,
	int end_line,
	const std::vector<codemap_symbol_info> &doc_symbols,
	agentlib::tool_context *ctx = nullptr,
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max());

// Identifies functions called by a specific symbol in safe_path.
std::vector<outgoing_call_reference> get_outgoing_calls_for_symbol(
	const std::string &safe_path,
	std::string_view symbol_name,
	agentlib::tool_context *ctx = nullptr);

// Query LSP (or regex fallback) and collect flat symbol list for a given file
std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, agentlib::tool_context &ctx, int min_lines = 1);
std::vector<codemap_symbol_info> get_document_codemap_symbols(const std::string &safe_path, int min_lines = 1);

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

// Resolve an outgoing call hierarchy item to its true definition file and bounds via LSP / dedicated codemap
bool resolve_outgoing_call_target(
	outgoing_call_reference &ref,
	const lsp_manager::call_hierarchy_item &item,
	std::unordered_map<std::string, std::vector<codemap_symbol_info>> &symbols_cache,
	agentlib::tool_context *ctx = nullptr);

// Find matching implementation file for a header file (e.g. .h -> .cpp, .hpp -> .cpp)
std::string find_matching_impl_file(const std::string &header_path, agentlib::tool_context *ctx = nullptr);
inline std::string find_matching_impl_file(const std::string &header_path, agentlib::tool_context &ctx) {
	return find_matching_impl_file(header_path, &ctx);
}

// Find matching header file for an implementation file (e.g. .cpp -> .h, .cpp -> .hpp)
std::string find_matching_header_file(const std::string &impl_path, agentlib::tool_context *ctx = nullptr);
inline std::string find_matching_header_file(const std::string &impl_path, agentlib::tool_context &ctx) {
	return find_matching_header_file(impl_path, &ctx);
}

// Extract mini class definition preview (referenced member variables and called member methods)
std::string extract_class_context_preview(
	const std::string &cpp_path,
	int start_line,
	int end_line,
	const std::vector<std::string> &read_lines,
	agentlib::tool_context &ctx);

// Find the innermost symbol enclosing line_number in a list of codemap symbols
const codemap_symbol_info* find_enclosing_symbol(const std::vector<codemap_symbol_info> &symbols, int line_number);

// Find a symbol in a list of codemap symbols matching a hint string (e.g. function or class name)
const codemap_symbol_info* find_symbol_by_hint(const std::vector<codemap_symbol_info> &symbols, std::string_view hint);

// Returns mini codemap annotation "[symbol: <name> (lines <start>-<end>)]" if resolved, or "" otherwise
std::string get_line_symbol_annotation(const std::vector<codemap_symbol_info> &symbols, int line_number);
std::string get_line_symbol_annotation(const std::string &safe_path, int line_number, agentlib::tool_context *ctx = nullptr);

// Augment up to max_annotations compiler error/warning lines in output with mini codemap annotations
std::string augment_compiler_output_with_codemap(const std::string &output, agentlib::tool_context *ctx = nullptr, size_t max_annotations = 3);

} // namespace tools
