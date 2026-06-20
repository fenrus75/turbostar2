#include "command_runner.h"
#include "fs_utils.h"
#include "security_scan_python.h"

namespace tools
{

security_scan_python_tool::security_scan_python_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Running Python security scan"), safe_paths_(std::move(safe_paths))
{
}

bool security_scan_python_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string security_scan_python_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	if (safe_paths_.empty()) {
		set_failure(ctx, "No files specified for scan.");
		return "Error: No files specified for scan.";
	}

	std::string cmd = "/usr/bin/bandit -q -f json";
	for (const auto &path : safe_paths_) {
		cmd += " " + fs_utils::escape_shell_arg(path);
	}

	sync_command_runner runner;
	runner.apply_build_profile();

	std::string output = runner.execute_and_get_output(cmd);
	int exit_code = runner.get_exit_code();

	if (output.empty()) {
		set_failure(ctx, "Bandit returned empty output.");
		return "Error: Bandit returned empty output.";
	}

	// Bandit returns 0 if no issues found, 1 if issues found.
	// Both are successful runs that return the JSON report.
	if (exit_code != 0 && exit_code != 1) {
		set_failure(ctx, "Bandit execution failed with exit code " + std::to_string(exit_code));
		return "Error: Bandit execution failed with exit code " + std::to_string(exit_code) + ".\nOutput:\n" + output;
	}

	set_success(ctx, "Scan completed successfully.");
	return output;
}

} // namespace tools
