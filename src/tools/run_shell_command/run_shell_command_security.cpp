#include "../../agentlib/ai_agent.h"
#include "../../agentlib/tool_registry.h"
#include "config_manager.h"
#include "run_shell_command.h"
#include "shell_command_resteer.h"

namespace tools
{

std::string run_shell_command_validator::get_description() const
{
	std::string desc = "Runs an arbitrary shell command safely within the sandbox. Requires explicit user permission approval and interrupts the agent flow. Do NOT use run_shell_command to read files (use fs_read_lines), search code (use fs_grep_files), list tests (read system://project/testlist.md via fs_read_lines), run unit tests (use fs_run_tests), or check git status/diff/files (use git_status, git_list_files, git_diff_unstaged, git_log).";
	if (config_manager::get_instance().is_allow_code_execution_network()) {
		desc += " Command execution has network access enabled.";
	} else {
		desc += " Command execution is strictly offline without network access.";
	}
	return desc;
}

bool run_shell_command_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const
{
    parsed_args_.command = args["command"].get<std::string>();
    
    if (args.contains("timeout") && args["timeout"].is_number_integer()) {
        parsed_args_.timeout = args["timeout"].get<int>();
    } else {
        parsed_args_.timeout = 300;
    }
    if (parsed_args_.timeout <= 0 || parsed_args_.timeout > 3600) {
        out_error = "Timeout must be between 1 and 3600 seconds.";
        return false;
    }

    if (args.contains("async") && args["async"].is_boolean()) {
        parsed_args_.is_async = args["async"].get<bool>();
    } else {
        parsed_args_.is_async = false;
    }

    if (args.contains("force") && args["force"].is_boolean()) {
        parsed_args_.force = args["force"].get<bool>();
    } else {
        parsed_args_.force = false;
    }

    if (parsed_args_.command.empty()) {
        out_error = "Command cannot be empty.";
        return false;
    }
    // Block ANSI escape sequences
    if (parsed_args_.command.find('\x1b') != std::string::npos) {
        out_error = "Command contains forbidden ANSI escape sequences.";
        return false;
    }

    // Evaluate regex re-steering engine if force is false
    if (!parsed_args_.force) {
        auto rec = evaluate_shell_command_resteer(parsed_args_.command);
        if (rec.matched && rec.confidence >= 0.90) {
            if (!rec.required_family.empty() && ctx.active_agent) {
                ctx.active_agent->add_active_tool_family(rec.required_family);
            }
            out_error = "Denied: Shell command matches native tool recommendation to prevent unnecessary user approval interrupts. " +
                        rec.explanation + " Recommended native call: " + rec.suggested_tool +
                        ". If native tools are genuinely insufficient, retry with force: true to request explicit user approval.";
            return false;
        }
    }

    return true;
}


std::unique_ptr<agentlib::llm_tool> run_shell_command_validator::create_tool_impl(const nlohmann::json& /*args*/) const
{
    return std::make_unique<run_shell_command_tool>(parsed_args_);
}

std::vector<agentlib::tool_example> run_shell_command_validator::get_examples() const
{
	return {
		{
			"Asynchronous Background Shell Command Execution",
			nlohmann::json{
				{"command", "ninja -C build"},
				{"async", true},
				{"timeout", 300}
			},
			"Launches long-running build command in background task. Note: Always prefer built-in tools (fs_grep_files over grep, git_* over git CLI, system://project/testlist.md over ctest) for speed, efficiency, and user approval bypass."
		},
		{
			"Forced Shell Execution with User Approval Bypass",
			nlohmann::json{
				{"command", "systemctl status myservice"},
				{"force", true}
			},
			"Requests explicit user security prompt approval for system administration task that lacks native tool equivalent."
		}
	};
}

REGISTER_TOOL(run_shell_command_validator)

} // namespace tools

