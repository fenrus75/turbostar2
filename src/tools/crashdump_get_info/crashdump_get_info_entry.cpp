#include "crashdump_manager.h"
#include "crashdump_get_info.h"
#include "fs_utils.h"
#include <filesystem>
#include <format>
#include <cstdlib>

namespace tools
{

static bool has_coredump(const std::string &crash_id)
{
	namespace fs = std::filesystem;
	std::string dump_dir_str = fs_utils::get_project_dump_dir();
	fs::path dump_dir = fs::path(dump_dir_str) / ("crash_" + crash_id);
	if (fs::exists(dump_dir)) {
		for (const auto &entry : fs::directory_iterator(dump_dir)) {
			if (entry.path().filename().string().starts_with("core")) {
				return true;
			}
		}
	}
	// Check systemd coredumpctl
	std::string cmd = std::format("coredumpctl --user info {} >/dev/null 2>&1", fs_utils::escape_shell_arg(crash_id));
	int exit_code = std::system(cmd.c_str());
	if (exit_code == 0) {
		return true;
	}
	return false;
}

crashdump_get_info_tool::crashdump_get_info_tool(crashdump_get_info_args args) : args_(std::move(args))
{
}

bool crashdump_get_info_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string crashdump_get_info_tool::execute(agentlib::tool_context & /*ctx*/)
{
	auto &mgr = crashdump_manager::get_instance();
	mgr.refresh();
	const auto &dumps = mgr.get_crashdumps();
	if (dumps.empty()) {
		return "Error: No crashdumps found.";
	}

	std::string target_id = args_.crash_id;
	if (target_id.empty()) {
		target_id = dumps.back().crash_id;
	}

	for (const auto &dump : dumps) {
		if (dump.crash_id == target_id) {
			std::string result = dump.raw_info;
			if (has_coredump(target_id)) {
				result += std::format(
					"\n\n### Optional: Interactive Coredump Debugging\n"
					"The backtrace and parameters above already show the crash location, call stack, and function arguments.\n"
					"If (and only if) the report above is insufficient to determine the root cause, you can launch an interactive GDB session to inspect deeper memory or local variables by calling 'agent_debug_coredump':\n"
					"```json\n"
					"{{\n"
					"  \"name\": \"agent_debug_coredump\",\n"
					"  \"arguments\": {{\n"
					"    \"crash_id\": \"{}\"\n"
					"  }}\n"
					"}}\n"
					"```\n",
					target_id
				);
			}
			if (result.length() > 20000) {
				result.resize(20000);
				result += "\n\n*(Output truncated at 20,000 characters)*\n";
			}
			return fs_utils::wrap_prompt_untrusted_data_tag("crashdump_get_info_result", result);
		}
	}
	return "Error: No crashdump found with ID " + target_id;
}

} // namespace tools