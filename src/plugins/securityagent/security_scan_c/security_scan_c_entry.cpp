#include "command_runner.h"
#include "fs_utils.h"
#include "security_scan_c.h"

namespace tools
{

security_scan_c_tool::security_scan_c_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Running C/C++ security scan"), safe_paths_(std::move(safe_paths))
{
}

bool security_scan_c_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string security_scan_c_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	if (safe_paths_.empty()) {
		set_failure(ctx, "No files specified for scan.");
		return "Error: No files specified for scan.";
	}

	std::string cmd = "/usr/bin/cppcheck -q --output-format=xmlv2";
	for (const auto &path : safe_paths_) {
		if (!fs_utils::is_regular_file(path)) {
			set_failure(ctx, "File is not a regular file: " + path);
			return "Error: File is not a regular file: " + path;
		}
		cmd += " " + fs_utils::escape_shell_arg(path);
	}

	sync_command_runner runner;
	runner.apply_strict_agent_profile();
	runner.set_timeout(60);

	std::string output = runner.execute_and_get_output(cmd);
	int exit_code = runner.get_exit_code();

	if (output.empty()) {
		set_failure(ctx, "cppcheck returned empty output.");
		return "Error: cppcheck returned empty output.";
	}

	if (exit_code != 0) {
		set_failure(ctx, "cppcheck execution failed with exit code " + std::to_string(exit_code));
		return fs_utils::wrap_prompt_untrusted_data_tag("cppcheck_scan_result", "Error: cppcheck execution failed with exit code " + std::to_string(exit_code) + ".\nOutput:\n" + output);
	}

	set_success(ctx, "Scan completed successfully.");
	if (output.size() > 32768) {
		output = output.substr(0, 32768) + "\n... (truncated)";
	}
	return fs_utils::wrap_prompt_untrusted_data_tag("cppcheck_scan_result", output);
}

} // namespace tools
