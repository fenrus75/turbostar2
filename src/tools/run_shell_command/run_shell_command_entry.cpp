#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <unordered_map>
#include "../../agentlib/tool_context.h"
#include "../../fs_utils.h"
#include "../terminal_command_runner.h"
#include "run_shell_command.h"

namespace tools
{

// In-memory session permission manager
static std::mutex g_perms_mutex;
static std::unordered_map<std::string, char> g_command_perms; // 'A' = always allow, 'D' = deny always

run_shell_command_tool::run_shell_command_tool(run_shell_command_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_terminal>("Shell Command", "Executing...");
}

std::shared_ptr<agentlib::agent_interaction> run_shell_command_tool::get_interaction() const
{
	return interaction_;
}

bool run_shell_command_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string run_shell_command_tool::execute(agentlib::tool_context &ctx)
{
	char rule = '?';
	{
		std::lock_guard<std::mutex> lock(g_perms_mutex);
		auto it = g_command_perms.find(args_.command);
		if (it != g_command_perms.end()) {
			rule = it->second;
		}
	}

	if (rule == 'D') {
		return "Error: Permission denied by user to run this command (Blacklisted).";
	}

	if (rule != 'A') {
		if (!ctx.queue) {
			return "Error: No event queue available to prompt the user for permission.";
		}

		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();

		editor_event ev;
		ev.type = event_type::prompt_user;
		ev.payload = "Agent wants to execute the following shell command:\n\n" + args_.command + "\n\nAllow execution?";
		ev.prompt_options = {"Once", "Always", "Deny Always", "Deny"};
		ev.prompt_promise = promise;

		ctx.queue->push(ev);

		std::string response;
		try {
			response = future.get();
		} catch (const std::exception &e) {
			return std::string("Error: Failed to get user response - ") + e.what();
		}

		if (response == "Deny") {
			return "Error: Permission denied by user for this request.";
		} else if (response == "Deny Always") {
			std::lock_guard<std::mutex> lock(g_perms_mutex);
			g_command_perms[args_.command] = 'D';
			return "Error: Permission denied by user (Blacklisted).";
		} else if (response == "Always") {
			std::lock_guard<std::mutex> lock(g_perms_mutex);
			g_command_perms[args_.command] = 'A';
		} else if (response != "Once") {
			return "Error: Unknown response from user.";
		}
	}

	// Permission granted
	auto runner = std::make_shared<terminal_command_runner>(interaction_, ctx.trigger_ui_update);
	runner->apply_strict_agent_profile();
	runner->set_enable_crash_catcher(true);
	runner->set_project_dir(ctx.fs_security.get_working_directory().string());
	runner->set_timeout(args_.timeout);

	if (args_.is_async) {
		std::thread([runner, cmd = args_.command]() {
			runner->execute(cmd);
			if (runner->get_final_output().empty()) {
				// The terminal interaction will have been updated by on_output_chunk, but we can append a completion message if needed, though usually empty is fine.
			}
		}).detach();
		return "Command started in background.";
	}

	// Execute directly: command_runner automatically escapes and wraps it via systemd-run ... -- bash -c '...'
	runner->execute(args_.command);

	std::string output = runner->get_final_output();

	if (output.empty()) {
		output = "Command finished successfully with no output.";
		if (interaction_) {
			interaction_->set_text(output);
			if (ctx.trigger_ui_update) {
				ctx.trigger_ui_update();
			}
		}
	}

	// Cap output at 20,000 characters to protect context window
	if (output.length() > 20000) {
		output = output.substr(output.length() - 20000);
		output = "\n...[output truncated due to length]...\n" + output;
	}

	return "```\n" + output + "\n```";
}

} // namespace tools