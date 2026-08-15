#include "apply_text_filter.h"
#include "agentlib/ai_agent.h"
#include "agentlib/virtual_file_system.h"
#include "filter_registry.h"
#include "fs_utils.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tools
{

apply_text_filter_tool::apply_text_filter_tool(apply_text_filter_args args)
    : args_(std::move(args))
{
}

bool apply_text_filter_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!args_.safe_output_path.empty() && !args_.safe_output_path.starts_with("tmp://") && !args_.safe_output_path.starts_with("images://")) {
		if (ctx.active_agent && ctx.active_agent->is_read_only()) {
			out_error = "Security Violation: Agent is in read-only mode and cannot write output file to workspace: " + args_.output_path;
			return false;
		}
	}
	return true;
}

std::string apply_text_filter_tool::execute(agentlib::tool_context &ctx)
{
	std::string input_text = args_.text;
	if (input_text.empty() && !args_.safe_path.empty()) {
		if (fs_utils::is_regular_file(args_.safe_path)) {
			std::error_code ec;
			auto file_sz = std::filesystem::file_size(args_.safe_path, ec);
			if (!ec && file_sz > 10485760) {
				return "Error: Input file '" + args_.path + "' exceeds maximum allowed size of 10MB.";
			}
		}
		std::ifstream ifs(args_.safe_path, std::ios::binary);
		if (!ifs.is_open()) {
			return "Error: Failed to open input file '" + args_.path + "' for reading.";
		}
		input_text = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		ifs.close();
	}

	bool success = false;
	std::string result = agentlib::filter_registry::get_instance().apply_filter(args_.filter, input_text, success);
	if (!success) {
		return "Error: Failed to apply filter '" + args_.filter + "'.";
	}

	if (!args_.safe_output_path.empty()) {
		bool is_vfs = (args_.safe_output_path.find("://") != std::string::npos);
		if (is_vfs) {
			auto vfs = ctx.fs_security.get_vfs();
			if (!vfs) {
				return "Error: VFS is not initialized in security context.";
			}
			std::string desc = vfs->write_file(args_.safe_output_path, result.data(), result.size());
			if (desc.empty()) {
				return "Error: Failed to write output to VFS path '" + args_.output_path + "'.";
			}
			return fs_utils::wrap_prompt_untrusted_data_tag("apply_text_filter_result", "Successfully applied filter '" + args_.filter + "' and wrote output to '" + args_.output_path + "'.");
		}

		std::ofstream out(args_.safe_output_path, std::ios::binary);
		if (!out.is_open()) {
			return "Error: Failed to open output path '" + args_.output_path + "' for writing.";
		}
		out << result;
		if (out.fail()) {
			return "Error: Failed to write filtered content to '" + args_.output_path + "'.";
		}
		return fs_utils::wrap_prompt_untrusted_data_tag("apply_text_filter_result", "Successfully applied filter '" + args_.filter + "' and wrote output to '" + args_.output_path + "'.");
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("apply_text_filter_result", result);
}

} // namespace tools
