#include "command_runner.h"
#include "fs_utils.h"
#include "security_verify_html.h"

namespace tools
{

security_verify_html_tool::security_verify_html_tool(std::vector<std::string> safe_paths)
    : llm_tool_action("Running HTML Tidy verification"), safe_paths_(std::move(safe_paths))
{
}

bool security_verify_html_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string security_verify_html_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	if (safe_paths_.empty()) {
		set_failure(ctx, "No files specified for HTML verification.");
		return "Error: No files specified for HTML verification.";
	}

	std::string cmd = "/usr/bin/tidy -eq";
	for (const auto &path : safe_paths_) {
		cmd += " " + fs_utils::escape_shell_arg(path);
	}

	sync_command_runner runner;
	runner.apply_build_profile();

	std::string output = runner.execute_and_get_output(cmd);
	int exit_code = runner.get_exit_code();

	// Tidy returns:
	// 0: success (no warnings or errors)
	// 1: warnings present
	// 2: errors present
	// Any other code indicates execution/internal failure.
	if (exit_code != 0 && exit_code != 1 && exit_code != 2) {
		set_failure(ctx, "HTML Tidy failed to run with exit code " + std::to_string(exit_code));
		return "Error: HTML Tidy execution failed with exit code " + std::to_string(exit_code) + ".\nOutput:\n" + output;
	}

	set_success(ctx, "HTML verification completed.");
	if (output.empty() && exit_code == 0) {
		return "HTML verification passed successfully with no errors or warnings.\n";
	}
	return output;
}

} // namespace tools
