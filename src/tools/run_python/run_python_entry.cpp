#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include "../../command_runner.h"
#include "../../crashdump_manager.h"
#include "../../fs_utils.h"
#include "../terminal_command_runner.h"
#include "run_python.h"

namespace tools
{

class live_python_runner : public terminal_command_runner
{
      public:
	live_python_runner(std::shared_ptr<agentlib::interaction_terminal> interaction, std::function<void()> trigger_update)
	    : terminal_command_runner(interaction, std::move(trigger_update))
	{
	}
};

run_python_tool::run_python_tool(run_python_args args) : args_(std::move(args))
{
	std::string title = "Python Execution";
	if (args_.file_path) {
		title += " (" + *args_.file_path + ")";
	}
	interaction_ = std::make_shared<agentlib::interaction_terminal>(title, "Running...");
}

std::shared_ptr<agentlib::agent_interaction> run_python_tool::get_interaction() const
{
	return interaction_;
}

bool run_python_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (args_.file_path) {
		std::string resolved_path;
		if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, out_error)) {
			return false;
		}
		if (!std::filesystem::exists(resolved_path)) {
			out_error = "File does not exist: " + resolved_path;
			return false;
		}
	}
	return true;
}

std::string run_python_tool::execute(agentlib::tool_context &ctx)
{
	bool bandit_installed = (system("which bandit > /dev/null 2>&1") == 0);
	std::string bandit_target_path;
	std::filesystem::path temp_file_path;

	if (args_.code) {
		if (bandit_installed) {
			static std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
			std::string temp_name = ".bandit_tmp_" + std::to_string(dist(rng)) + ".py";
			temp_file_path = ctx.fs_security.get_working_directory() / temp_name;

			std::ofstream out(temp_file_path);
			if (!out) {
				return "Execution Error: Failed to create temporary file for security check.";
			}
			out << *args_.code;
			out.close();
			bandit_target_path = temp_file_path.string();
		}
	} else {
		std::string resolved_path;
		std::string error;
		if (!ctx.fs_security.validate_access(*args_.file_path, agentlib::access_type::read, resolved_path, error)) {
			return "Error: " + error;
		}
		bandit_target_path = resolved_path;
	}

	// RAII helper to clean up the temporary file if one was created
	struct temp_cleanup {
		std::filesystem::path path;
		~temp_cleanup()
		{
			if (!path.empty()) {
				std::error_code ec;
				std::filesystem::remove(path, ec);
			}
		}
	} guard{temp_file_path};

	if (bandit_installed && !bandit_target_path.empty()) {
		sync_command_runner bandit_runner;
		bandit_runner.apply_build_profile();
		bandit_runner.set_project_dir(ctx.fs_security.get_working_directory().string());

		// SECURE-BY-DESIGN: The path is automatically escaped by execute_and_get_output when formatting.
		std::string bandit_output = bandit_runner.execute_and_get_output("bandit --severity-level=high {} 2>&1", bandit_target_path);
		int exit_code = bandit_runner.get_exit_code();

		if (exit_code != 0) {
			std::string error_msg = "Security Validation Failed: Bandit detected high-severity issues in the Python code:\n\n";
			error_msg += bandit_output;
			return error_msg;
		}
	}

	live_python_runner runner(interaction_, ctx.trigger_ui_update);
	runner.apply_strict_agent_profile();
	runner.set_enable_crash_catcher(true);
	runner.set_project_dir(ctx.fs_security.get_working_directory().string());

	// Allow uv cache explicitly if it exists
	const char *home = std::getenv("HOME");
	if (home) {
		std::string uv_cache = std::string(home) + "/.cache/uv";
		if (std::filesystem::exists(uv_cache)) {
			runner.add_extra_rw_path(uv_cache);
		}
	}

	std::string script_path;

	if (args_.code) {
		// We will use stdin
	} else {
		// The path is already validated and resolved above in resolved_path/bandit_target_path
		script_path = bandit_target_path;
	}

	std::string base_cmd;

	// Check if uv is available
	if (system("which uv > /dev/null 2>&1") == 0) {
		base_cmd = "PYTHONUNBUFFERED=1 uv run ";
		for (const auto &dep : args_.dependencies) {
			base_cmd += "--with " + fs_utils::escape_shell_arg(dep) + " ";
		}
	} else {
		base_cmd = "PYTHONUNBUFFERED=1 python3 -u ";
		if (!args_.dependencies.empty()) {
			return "Execution Error: Dependencies were requested but 'uv' is not installed on the host system.";
		}
	}

	std::string full_cmd;
	if (args_.code) {
		// Execute via stdin
		base_cmd += "- "; // Python / uv reads from stdin
		full_cmd = "echo " + fs_utils::escape_shell_arg(*args_.code) + " | " + base_cmd;
		
		if (interaction_) {
			std::string display_cmd = base_cmd + "<inline code>";
			if (display_cmd.find("PYTHONUNBUFFERED=1 ") == 0) {
				display_cmd = display_cmd.substr(19);
			}
			std::dynamic_pointer_cast<agentlib::interaction_terminal>(interaction_)->set_title(display_cmd);
		}
	} else {
		full_cmd = base_cmd + fs_utils::escape_shell_arg(script_path);
		
		if (interaction_) {
			std::string display_cmd = base_cmd + *args_.file_path;
			if (display_cmd.find("PYTHONUNBUFFERED=1 ") == 0) {
				display_cmd = display_cmd.substr(19);
			}
			std::dynamic_pointer_cast<agentlib::interaction_terminal>(interaction_)->set_title(display_cmd);
		}
	}

	size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
	runner.execute(full_cmd);

	std::string output = runner.get_final_output();
	runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
	size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

	if (crashes_after > crashes_before) {
		output += "\n\nCRASH DETECTED: " + std::to_string(crashes_after - crashes_before) +
			  " new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to "
			  "investigate.";
	}

	if (output.empty()) {
		output = "Process finished successfully with no output.";
		if (interaction_) {
			interaction_->set_text(output);
			if (ctx.trigger_ui_update) {
				ctx.trigger_ui_update();
			}
		}
	}

	return output;
}

} // namespace tools