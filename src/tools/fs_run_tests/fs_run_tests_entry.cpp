#include "../../config_manager.h"
#include "../../crashdump_manager.h"
#include "../../fs_utils.h"
#include "../../project_manager.h"
#include "../../agentlib/ai_agent.h"
#include "../terminal_command_runner.h"
#include "../output_filter.h"
#include "fs_run_tests.h"

namespace tools
{

fs_run_tests_tool::fs_run_tests_tool(std::vector<std::string> test_names)
    : test_names_(std::move(test_names))
{
	interaction_ = std::make_shared<agentlib::interaction_terminal>("Test Suite", "Running tests...");
}

std::shared_ptr<agentlib::agent_interaction> fs_run_tests_tool::get_interaction() const
{
	return interaction_;
}

bool fs_run_tests_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string fs_run_tests_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	terminal_command_runner runner(interaction_, ctx.trigger_ui_update);
	runner.set_enable_crash_catcher(true);
	runner.set_project_dir(ctx.fs_security.get_working_directory().string());

	std::string build_system = config_manager::get_instance().get_build_system();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	
	std::string repo_root = project_manager::get_instance().get_repository_root();
	std::filesystem::path build_path(build_dir);
	if (build_path.is_relative()) {
		build_path = std::filesystem::path(repo_root) / build_path;
	}

	std::string cmd;

	if (build_system == "meson") {
		cmd = "MESON_TESTTHREADS=2 meson test -C " + build_path.string();
		for (const auto &t : test_names_) {
			cmd += " " + t;
		}
	} else if (build_system == "cmake") {
		cmd = "ctest --test-dir " + build_path.string();
		if (!test_names_.empty()) {
			cmd += " -R \"(";
			for (size_t i = 0; i < test_names_.size(); ++i) {
				cmd += test_names_[i];
				if (i < test_names_.size() - 1)
					cmd += "|";
			}
			cmd += ")\"";
		}
	} else if (build_system == "make") {
		cmd = "make test -C " + build_dir;
		// Make doesn't have a standard way to run individual tests via 'make test'
	} else {
		cmd = build_system + " test " + build_dir; // Fallback
	}

	size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
	runner.execute(cmd);

	std::string output = runner.get_final_output();
	runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
	size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

	// Apply output filters to summarize/prune execution logs proactively
	std::vector<std::shared_ptr<output_filter>> filters;
	filters.push_back(std::make_shared<meson_test_filter>());
	int lines_removed = 0;
	output = apply_output_filters(cmd, output, filters, &lines_removed);

	if (lines_removed > 0 && ctx.active_agent) {
		ctx.active_agent->increment_stat("test_lines_pruned", lines_removed);
	}

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