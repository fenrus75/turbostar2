#include <iomanip>
#include <sstream>
#include "../../build_error_manager.h"
#include "../../config_manager.h"
#include "../../fs_utils.h"
#include "fs_compile_info.h"

namespace tools
{

fs_compile_info_tool::fs_compile_info_tool(std::string safe_path, std::string requested_path)
    : safe_path_(std::move(safe_path)), requested_path_(std::move(requested_path))
{
}

bool fs_compile_info_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_compile_info_tool::escape_markdown(const std::string &text) const
{
	std::string result;
	for (char c : text) {
		if (c == '|') {
			result += "&#124;"; // HTML entity for pipe
		} else if (c == '\r' || c == '\n') {
			result += " "; // Replace newlines with space for table rows
		} else {
			result += c;
		}
	}
	return result;
}

std::string fs_compile_info_tool::execute(agentlib::tool_context &ctx)
{
	std::stringstream ss;
	ss << "# Compile Information for `" << requested_path_ << "`\n\n";

	// 1. Last Compiled Time
	std::time_t last_time = build_error_manager::get_instance().get_last_compile_time();
	ss << "## Last Compiled\n";
	if (last_time == 0) {
		ss << "Not available\n\n";
	} else {
		std::tm *tm_info = std::localtime(&last_time);
		ss << std::put_time(tm_info, "%Y-%m-%d %H:%M:%S") << "\n\n";
	}

	// 2. Compilation Command
	std::string build_dir = config_manager::get_instance().get_build_directory();
	std::string cmd = fs_utils::get_compile_command_for_file(safe_path_, build_dir);
	ss << "## Compilation Command\n";
	if (cmd.empty()) {
		ss << "*(Not available in compile_commands.json)*\n\n";
	} else {
		ss << "```bash\n" << cmd << "\n```\n\n";
	}

	// 3. Compiler Diagnostics
	ss << "## Compiler Diagnostics (from last build)\n";
	const auto &errors = build_error_manager::get_instance().get_errors();
	bool found_compile_errors = false;

	std::stringstream compile_table;
	compile_table << "| Line | Column | Severity | Message |\n";
	compile_table << "| ---- | ------ | -------- | ------- |\n";

	for (const auto &err : errors) {
		// Simple string match on the filename. The error manager stores what gcc outputted,
		// which could be relative or absolute.
		if (err.filepath.find(requested_path_) != std::string::npos || safe_path_.find(err.filepath) != std::string::npos) {
			found_compile_errors = true;
			compile_table << "| " << err.line << " | " << err.column << " | " << (err.is_warning ? "Warning" : "Error") << " | "
				      << escape_markdown(err.message) << " |\n";
		}
	}

	if (found_compile_errors) {
		ss << compile_table.str() << "\n";
	} else {
		ss << "*(No compiler diagnostics available for this file)*\n\n";
	}

	// 4. LSP Diagnostics
	ss << "## LSP Diagnostics (Live)\n";
	bool found_lsp_diagnostics = false;

	if (ctx.doc_provider) {
		auto doc = ctx.doc_provider->get_open_document(safe_path_);
		if (doc) {
			auto diagnostics = doc->get_diagnostics();
			if (!diagnostics.empty()) {
				found_lsp_diagnostics = true;
				ss << "| Line | Column | Severity | Source | Message |\n";
				ss << "| ---- | ------ | -------- | ------ | ------- |\n";
				for (const auto &d : diagnostics) {
					ss << "| " << (d.line + 1) << " | " << (d.column + 1) << " | " << d.severity << " | " << d.source
					   << " | " << escape_markdown(d.message) << " |\n";
				}
			}
		}
	}

	if (!found_lsp_diagnostics) {
		ss << "*(No live LSP diagnostics available. File might be clean or not currently open.)*\n";
	}

	return ss.str();
}

} // namespace tools
