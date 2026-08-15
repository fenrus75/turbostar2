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
	const auto &dumps = crashdump_manager::get_instance().get_crashdumps();
	for (const auto &dump : dumps) {
		if (dump.crash_id == args_.crash_id) {
			std::string result = dump.raw_info;
			if (has_coredump(args_.crash_id)) {
				result += std::format(
					"\n\n### Coredump Debugging\n"
					"A coredump is available for this crash. You can launch a GDB session to debug this coredump by calling the 'agent_debug_coredump' tool:\n"
					"```json\n"
					"{{\n"
					"  \"name\": \"agent_debug_coredump\",\n"
					"  \"arguments\": {{\n"
					"    \"crash_id\": \"{}\"\n"
					"  }}\n"
					"}}\n"
					"```\n",
					args_.crash_id
				);
			}
			if (result.length() > 20000) {
				result.resize(20000);
				result += "\n\n*(Output truncated at 20,000 characters)*\n";
			}
			return fs_utils::wrap_prompt_untrusted_data_tag("crashdump_get_info_result", result);
		}
	}
	return "Error: No crashdump found with ID " + args_.crash_id;
}

} // namespace tools