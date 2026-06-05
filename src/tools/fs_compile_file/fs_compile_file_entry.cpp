#include "../../config_manager.h"
#include "../../crashdump_manager.h"
#include "../../fs_utils.h"
#include "../terminal_command_runner.h"
#include "fs_compile_file.h"
#include "../../agentlib/ai_agent.h"
#include <thread>
#include <format>

namespace tools
{

fs_compile_file_tool::fs_compile_file_tool(fs_compile_file_args args, std::string safe_path)
	: args_(std::move(args)), safe_path_(std::move(safe_path))
{
	interaction_ = std::make_shared<agentlib::interaction_terminal>("Compile File", "Compiling " + safe_path_ + "...");
}

std::shared_ptr<agentlib::agent_interaction> fs_compile_file_tool::get_interaction() const
{
	return interaction_;
}

bool fs_compile_file_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_compile_file_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	std::string build_dir = config_manager::get_instance().get_build_directory();
	std::string cmd = fs_utils::get_compile_command_for_file(safe_path_, build_dir);

	if (cmd.empty()) {
		return "Error: Cannot find compile command for this file in compile_commands.json.";
	}

	cmd = "export LC_ALL=C.UTF-8 LANG=C.UTF-8 && " + cmd;

	if (args_.async) {
		std::weak_ptr<agentlib::ai_agent> weak_agent;
		if (ctx.active_agent) {
			weak_agent = ctx.active_agent->shared_from_this();
		}
		std::string captured_tool_call_id = ctx.tool_call_id;

		std::thread([safe_path = safe_path_, runner = std::make_shared<terminal_command_runner>(interaction_, ctx.trigger_ui_update), cmd, weak_agent, captured_tool_call_id, workspace_dir = ctx.fs_security.get_working_directory().string()]() {
			runner->set_enable_crash_catcher(true);
			runner->set_project_dir(workspace_dir);

			size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
			int exit_code = runner->execute(cmd);

			std::string output = runner->get_final_output();
			runner->get_new_crashdumps(); // Trigger refresh in the runner to update the manager
			size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

			if (crashes_after > crashes_before) {
				output += "\n\nCRASH DETECTED: " + std::to_string(crashes_after - crashes_before) +
					  " new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to "
					  "investigate.";
			}

			// Cap output at 10,000 characters to protect context window
			if (output.length() > 10000) {
				output = output.substr(output.length() - 10000);
				output = "\n...[output truncated due to length]...\n" + output;
			}

			std::string formatted_injection = "```bash\n$ " + cmd + "\n" + output + "\n```";

			if (auto agent = weak_agent.lock()) {
				agent->replace_tool_result(captured_tool_call_id, formatted_injection);
				std::string status = (exit_code == 0) ? "successfully" : "with errors";
				std::string system_msg = std::format(
					"The background task 'fs_compile_file' ({}) has completed {}. I updated your previous tool result with the output.",
					safe_path, status);
				agent->inject_context("system", system_msg, true);
			}
		}).detach();

		return "Compilation started in the background. The output will be injected here when it completes.";
	}

	terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
	runner.set_enable_crash_catcher(true);
	runner.set_project_dir(ctx.fs_security.get_working_directory().string());

	size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
	runner.execute(cmd);

	std::string output = runner.get_final_output();
	runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
	size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

	if (crashes_after > crashes_before) {
		output += "\n\nCRASH DETECTED: " + std::to_string(crashes_after - crashes_before) +
			  " new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to "
			  "investigate.";
	}

	// Cap output at 10,000 characters to protect context window
	if (output.length() > 10000) {
		output = output.substr(output.length() - 10000);
		output = "\n...[output truncated due to length]...\n" + output;
	}

	return "```bash\n$ " + cmd + "\n" + output + "\n```";
}

} // namespace tools
