#include "command_runner.h"
#include "fs_utils.h"
#include "security_scan_semgrep.h"

namespace tools
{

security_scan_semgrep_tool::security_scan_semgrep_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Running Semgrep security scan"), safe_paths_(std::move(safe_paths))
{
}

bool security_scan_semgrep_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string security_scan_semgrep_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	if (safe_paths_.empty()) {
		set_failure(ctx, "No files specified for scan.");
		return "Error: No files specified for scan.";
	}

	std::string cmd = "/usr/bin/uvx -q semgrep scan --config=auto --json --quiet --config \"p/security-audit\" --config \"p/secrets\"";
	for (const auto &path : safe_paths_) {
		cmd += " " + fs_utils::escape_shell_arg(path);
	}
	cmd += " 2>/dev/null";

	sync_command_runner runner;
	runner.apply_build_profile();
	runner.set_home_access(home_access_t::read_write);

	std::string output = runner.execute_and_get_output(cmd);
	int exit_code = runner.get_exit_code();

	if (output.empty()) {
		set_failure(ctx, "Semgrep returned empty output.");
		return "Error: Semgrep returned empty output.";
	}

	if (exit_code != 0) {
		set_failure(ctx, "Semgrep execution failed with exit code " + std::to_string(exit_code));
		return "Error: Semgrep execution failed with exit code " + std::to_string(exit_code) + ".\nOutput:\n" + output;
	}

	set_success(ctx, "Scan completed successfully.");
	return output;
}

} // namespace tools
