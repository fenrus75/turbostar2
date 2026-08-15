#pragma once

#include "agentlib/tool_context.h"
#include "codemap_utils.h"
#include <string>
#include <vector>

namespace tools {

struct replace_chunk {
	std::string target_content;
	std::string replacement_content;
	int line_hint{0};            // 1-based line number hint (optional)
	std::string function_scope; // Enclosing function/class hint name (optional)
	int start_line{0};          // 1-based start line window (optional)
	int end_line{0};            // 1-based end line window (optional)
};

struct replace_engine_args {
	std::string path;           // Display path
	std::string safe_path;      // Resolved safe path
	std::vector<replace_chunk> chunks;
	bool strict{false};         // If true, reject edit if brace balance is broken
};

struct replace_engine_result {
	bool success{false};
	std::string error_message;
	std::string warning_message;
	std::string result_text;
	std::vector<std::string> before_lines;
	std::vector<std::string> after_lines;
	int chunks_applied{0};
};

class fs_replace_engine {
public:
	// Executes atomic single-chunk or multi-chunk file replacements against disk/VFS
	static replace_engine_result execute(agentlib::tool_context &ctx, const replace_engine_args &args);

	// Helper for calculating brace balance (+1 for {, -1 for }) respecting multiline strings and comments
	static int calculate_brace_balance(const std::vector<std::string> &lines, int start_line_0, int end_line_0);

	// Generates warning/error text if the edit broke brace balance at file scope or function scope
	static std::string check_brace_warnings(
		const std::string &safe_path,
		agentlib::tool_context &ctx,
		const std::vector<std::string> &before_lines,
		const std::vector<std::string> &after_lines,
		const std::vector<std::pair<int, int>> &edited_ranges_0based,
		int net_line_delta);
};

} // namespace tools
