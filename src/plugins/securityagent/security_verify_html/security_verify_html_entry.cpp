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
		if (!fs_utils::is_regular_file(path)) {
			set_failure(ctx, "File is not a regular file: " + path);
			return "Error: File is not a regular file: " + path;
		}
		cmd += " " + fs_utils::escape_shell_arg(path);
	}

	sync_command_runner runner;
	runner.apply_strict_agent_profile();

	std::string output = runner.execute_and_get_output(cmd);
	int exit_code = runner.get_exit_code();

	if (exit_code != 0 && exit_code != 1 && exit_code != 2) {
		set_failure(ctx, "HTML Tidy failed to run with exit code " + std::to_string(exit_code));
		return fs_utils::wrap_prompt_untrusted_data_tag("html_verify_result", "Error: HTML Tidy execution failed with exit code " + std::to_string(exit_code) + ".\nOutput:\n" + output);
	}

	set_success(ctx, "HTML verification completed.");
	if (output.empty() && exit_code == 0) {
		return fs_utils::wrap_prompt_untrusted_data_tag("html_verify_result", "HTML verification passed successfully with no errors or warnings.\n");
	}
	if (output.size() > 32768) {
		output = output.substr(0, 32768) + "\n... (truncated)";
	}
	return fs_utils::wrap_prompt_untrusted_data_tag("html_verify_result", output);
}

} // namespace tools
