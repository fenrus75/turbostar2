#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "agentlib/llm_tool.h"

class mapped_file;

namespace agentlib
{
class document_snapshot;
class vfs_content_buffer;
class virtual_file_system;
struct tool_context;
} // namespace agentlib

namespace tools
{

struct codemap_symbol_info;

struct fs_read_lines_args {
	std::string requested_path;
	int start_line; // 1-based index
	int end_line;	// 1-based index
	std::optional<int> tail;
	std::optional<int> length;
	std::string safe_path; // Injected by Stage 1 validation
};

struct file_read_result {
	bool success = false;
	std::string error_message;
	std::shared_ptr<const mapped_file> mapped_backing;
	std::shared_ptr<const agentlib::vfs_content_buffer> vfs_backing;
	std::vector<std::string> owned_lines;
	std::vector<std::string_view> lines;
	std::string_view raw_range_view;
	size_t total_file_lines = 0;
	int start_line = 1;
	int end_line = 1;
};

// Reads lines from a file (supporting VFS, open document snapshots, or local disk),
// optionally applying boundary heuristics.
file_read_result read_file_lines(const std::string &safe_path, const std::string &requested_path, int start_line, int end_line,
				 std::optional<int> tail, agentlib::tool_context &ctx, bool adjust_boundaries = true,
				 std::vector<codemap_symbol_info> *out_symbols = nullptr, bool *out_symbols_loaded = nullptr);

// Formats file_read_result as standard markdown fenced code block with line numbers.
std::string format_file_lines_markdown(const std::string &requested_path, const file_read_result &read_res);

class fs_read_lines_tool : public agentlib::llm_tool
{
      public:
	explicit fs_read_lines_tool(fs_read_lines_args args);

	std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
	bool validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const override;
	std::string execute(agentlib::tool_context &ctx) override;

      private:
	fs_read_lines_args args_;
	std::shared_ptr<agentlib::agent_interaction> interaction_;
};

} // namespace tools
