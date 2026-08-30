#include <format>
#include <thread>
#include <sstream>
#include "agentlib/ai_agent.h"
#include "config_manager.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "tools/output_filter.h"
#include "tools/terminal_command_runner.h"
#include "tools/fs_compile_project/fs_compile_project.h"
#include "codemap_utils.h"
#include "event_logger.h"

namespace tools
{

fs_compile_project_tool::fs_compile_project_tool(fs_compile_project_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_terminal>("Compilation", "Compiling project...");
}

std::shared_ptr<agentlib::agent_interaction> fs_compile_project_tool::get_interaction() const
{
	return interaction_;
}

bool fs_compile_project_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_compile_project_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	std::string build_system = config_manager::get_instance().get_build_system();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	std::string cmd;

	if (build_system == "meson") {
		cmd = "meson compile -C " + build_dir;
		if (args_.clean) {
			cmd = "ninja -C " + build_dir + " clean && " + cmd;
		}
	} else if (build_system == "cmake") {
		cmd = "cmake --build " + build_dir;
		if (args_.clean) {
			cmd = "cmake --build " + build_dir + " --target clean && " + cmd;
		}
	} else if (build_system == "make") {
		cmd = "make -C " + build_dir;
		if (args_.clean) {
			cmd = "make -C " + build_dir + " clean && " + cmd;
		}
	} else {
		cmd = build_system + " " + build_dir; // Fallback
	}

	cmd = "export LC_ALL=C.UTF-8 LANG=C.UTF-8 && " + cmd;

	bool async_enabled = args_.async;
	if (ctx.active_agent && !ctx.active_agent->is_mutation_possible()) {
		async_enabled = false;
	}

	if (async_enabled) {
		std::weak_ptr<agentlib::ai_agent> weak_agent;
		if (ctx.active_agent) {
			weak_agent = ctx.active_agent->shared_from_this();
		}
		std::string captured_tool_call_id = ctx.tool_call_id;

		std::thread([runner = std::make_shared<terminal_command_runner>(interaction_, ctx.trigger_ui_update), cmd, weak_agent,
			     captured_tool_call_id, workspace_dir = ctx.fs_security.get_working_directory().string(), timeout = args_.timeout]() {
			runner->set_enable_crash_catcher(true);
			runner->set_project_dir(workspace_dir);
			runner->set_timeout(timeout);

			size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
			int exit_code = runner->execute(cmd);

			std::string output = runner->get_final_output();
			runner->get_new_crashdumps(); // Trigger refresh in the runner to update the manager
			size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

			// Apply output filters to summarize/prune execution logs proactively
			std::vector<std::shared_ptr<output_filter>> filters;
			filters.push_back(std::make_shared<meson_compile_filter>());
			int lines_removed = 0;
			size_t raw_bytes = output.length();
			output = apply_output_filters(cmd, output, filters, &lines_removed);
			size_t kept_bytes = output.length();

			int lines_kept = 0;
			{
				std::stringstream ss(output);
				std::string tmp;
				while (std::getline(ss, tmp)) {
					lines_kept++;
				}
			}
			double raw_kb = raw_bytes / 1024.0;
			double kept_kb = kept_bytes / 1024.0;
			event_logger::get_instance().log("fs_compile_project context minimizer: removed {} lines ({:.1f} KB), kept {} lines ({:.1f} KB)", lines_removed, raw_kb - kept_kb, lines_kept, kept_kb);

			if (auto agent = weak_agent.lock()) {
				if (lines_removed > 0) {
					agent->increment_stat("build_lines_pruned", lines_removed);
				}

				if (crashes_after > crashes_before) {
					output += std::format("\n\nCRASH DETECTED: {} new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to investigate.", crashes_after - crashes_before);
				}

				output = augment_compiler_output_with_codemap(output, nullptr, 3);
				// Cap output at 10,000 characters to protect context window
				if (output.length() > 10000) {
					output = output.substr(output.length() - 10000);
					output = "\n...[output truncated due to length]...\n" + output;
				}

				std::string summary_header;
				if (lines_removed > 0) {
					summary_header = std::format("NOTE: Build output summarized to save context tokens (filtered {} low-information log lines). Full error messages and warnings are preserved.\n\n", lines_removed);
				}

				std::string formatted_injection = std::format("{}```bash\n$ {}\n{}\n```", summary_header, cmd, output);
				agent->replace_tool_result(captured_tool_call_id, formatted_injection);
				std::string status = (exit_code == 0) ? "successfully" : "with errors";
				std::string system_msg = std::format("The background task 'fs_compile_project' has completed {}. I updated "
								     "your previous tool result with the output.",
								     status);
				agent->inject_untrusted_context("system", system_msg, true);
			}
		}).detach();

		return "Project compilation started in the background. The output will be injected here when it completes.";
	}

	terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
	runner.set_enable_crash_catcher(true);
	runner.set_timeout(args_.timeout);
	runner.set_project_dir(ctx.fs_security.get_working_directory().string());

	size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
	runner.execute(cmd);

	std::string output = runner.get_final_output();
	runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
	size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

	// Apply output filters to summarize/prune execution logs proactively
	std::vector<std::shared_ptr<output_filter>> filters;
	filters.push_back(std::make_shared<meson_compile_filter>());
	int lines_removed = 0;
	size_t raw_bytes = output.length();
	output = apply_output_filters(cmd, output, filters, &lines_removed);
	size_t kept_bytes = output.length();

	int lines_kept = 0;
	{
		std::stringstream ss(output);
		std::string tmp;
		while (std::getline(ss, tmp)) {
			lines_kept++;
		}
	}
	double raw_kb = raw_bytes / 1024.0;
	double kept_kb = kept_bytes / 1024.0;
	event_logger::get_instance().log("fs_compile_project context minimizer: removed {} lines ({:.1f} KB), kept {} lines ({:.1f} KB)", lines_removed, raw_kb - kept_kb, lines_kept, kept_kb);

	if (lines_removed > 0 && ctx.active_agent) {
		ctx.active_agent->increment_stat("build_lines_pruned", lines_removed);
	}

	if (crashes_after > crashes_before) {
		output += std::format("\n\nCRASH DETECTED: {} new crash(es) occurred during execution. Please use the 'crashdump_list' and 'crashdump_get_info' tools to investigate.", crashes_after - crashes_before);
	}

	output = augment_compiler_output_with_codemap(output, &ctx, 3);
	// Cap output at 10,000 characters to protect context window
	if (output.length() > 10000) {
		output = output.substr(output.length() - 10000);
		output = "\n...[output truncated due to length]...\n" + output;
	}

	std::string summary_header;
	if (lines_removed > 0) {
		summary_header = std::format("NOTE: Build output summarized to save context tokens (filtered {} low-information log lines). Full error messages and warnings are preserved.\n\n", lines_removed);
	}

	return std::format("{}```bash\n$ {}\n{}\n```", summary_header, cmd, output);
}

} // namespace tools
