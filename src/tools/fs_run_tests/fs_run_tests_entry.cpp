#include <algorithm>
#include <cctype>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "config_manager.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "agentlib/ai_agent.h"
#include "tools/terminal_command_runner.h"
#include "tools/output_filter.h"
#include "fs_run_tests.h"

namespace tools
{

// Case-insensitive substring match used to resolve a partial test name against the full names
// returned by project_manager::get_available_tests().
static bool contains_case_insensitive(std::string_view haystack, std::string_view needle)
{
	if (needle.empty()) {
		return true;
	}
	auto to_lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
	std::string hay(haystack);
	std::string ned(needle);
	std::transform(hay.begin(), hay.end(), hay.begin(), to_lower);
	std::transform(ned.begin(), ned.end(), ned.begin(), to_lower);
	return hay.find(ned) != std::string::npos;
}

// Resolves the requested test names (which may be exact full names or substrings) against the
// project's current available-test list. Returns the concrete test names to run. Sets
// did_substring_expand to true if at least one requested name was a substring expansion rather
// than an exact match.
static std::vector<std::string> resolve_test_names(std::span<const std::string> test_names,
						    bool *did_substring_expand)
{
	std::vector<std::string> resolved;
	const std::vector<std::string> available = project_manager::get_instance().get_available_tests();
	for (const auto &t : test_names) {
		bool matched = false;
		for (const auto &candidate : available) {
			if (candidate == t) {
				if (std::find(resolved.begin(), resolved.end(), t) == resolved.end()) {
					resolved.push_back(t);
				}
				matched = true;
				break;
			}
		}
		if (matched) {
			continue; // exact full-name match: use it verbatim
		}
		// No exact match: expand the substring to every available test containing it.
		for (const auto &candidate : available) {
			if (contains_case_insensitive(candidate, t)) {
				if (std::find(resolved.begin(), resolved.end(), candidate) == resolved.end()) {
					resolved.push_back(candidate);
				}
				if (did_substring_expand) {
					*did_substring_expand = true;
				}
			}
		}
	}
	return resolved;
}

fs_run_tests_tool::fs_run_tests_tool(std::vector<std::string> test_names, int timeout)
    : test_names_(std::move(test_names)), timeout_(timeout)
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
	runner.set_timeout(timeout_);
	runner.set_project_dir(ctx.fs_security.get_working_directory().string());
	if (config_manager::get_instance().is_run_outside_sandbox()) {
		runner.set_bypass_sandbox(true);
	}

	std::string build_system = config_manager::get_instance().get_build_system();
	std::string build_dir = config_manager::get_instance().get_build_directory();
	
	std::string proj_root = project_manager::get_instance().get_project_root();
	std::filesystem::path build_path(build_dir);
	if (build_path.is_relative()) {
		build_path = std::filesystem::path(proj_root) / build_path;
	}
	if (!std::filesystem::exists(build_path / "build.ninja") && std::filesystem::exists(std::filesystem::path(proj_root) / "build.ninja")) {
		build_path = proj_root;
	}

	// Resolve each requested test name against the project's available-test list. The agent
	// may pass a substring of the full test name (e.g. "run_shell_command" instead of
	// "turbostar:unit_test_run_shell_command"). We prefer the exact full name, but if no
	// exact match exists, we expand substrings to every matching test so the run is still useful
	// and the output shows the agent exactly which tests matched.
	std::vector<std::string> resolved;
	// Flag that we expanded one or more substrings to multiple concrete tests. In that case we
	// want to leave the output unfiltered below so the agent sees every test that ran and can
	// tell exactly which (potentially several) tests a substring matched.
	bool did_substring_expand = false;
	if (test_names_.empty()) {
		resolved = test_names_;
	} else {
		resolved = resolve_test_names(test_names_, &did_substring_expand);
		// If nothing resolved, the in-memory test list may be stale (e.g. tests were added to
		// meson.build after the editor populated the cache). Invalidate the cache so the next
		// get_available_tests() forces a real refresh, then retry the resolution once before
		// giving up. This makes newly-registered tests discoverable without an editor restart.
		if (resolved.empty()) {
			project_manager::get_instance().invalidate_available_tests_cache();
			resolved = resolve_test_names(test_names_, &did_substring_expand);
		}
	}

	std::string cmd;

	if (build_system == "meson") {
		cmd = "MESON_TESTTHREADS=2 meson test -C " + build_path.string();
		for (const auto &t : resolved) {
			cmd += " " + fs_utils::escape_shell_arg(t);
		}
	} else if (build_system == "cmake") {
		cmd = "ctest --test-dir " + build_path.string();
		if (!resolved.empty()) {
			cmd += " -R \"(";
			for (size_t i = 0; i < resolved.size(); ++i) {
				cmd += resolved[i];
				if (i < resolved.size() - 1)
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

	// If the agent passed specific test names but none resolved, surface that clearly with a
	// pointer to the discovery file rather than silently matching nothing.
	if (!test_names_.empty() && resolved.empty()) {
		std::string names;
		for (size_t i = 0; i < test_names_.size(); ++i) {
			if (i > 0)
				names += ", ";
			names += "'" + test_names_[i] + "'";
		}
		return "No tests matched the requested name(s): " + names +
		       ". Read system://project/testlist.md to discover valid test names.\n";
	}

	size_t crashes_before = crashdump_manager::get_instance().get_crashdumps().size();
	runner.execute(cmd);

	std::string output = runner.get_final_output();
	runner.get_new_crashdumps(); // Trigger refresh in the runner to update the manager
	size_t crashes_after = crashdump_manager::get_instance().get_crashdumps().size();

	// Apply output filters to summarize/prune execution logs proactively. Skip pruning when we
	// expanded a substring to multiple tests, so the full (unfiltered) output shows the agent
	// exactly which tests were matched and ran.
	std::vector<std::shared_ptr<output_filter>> filters;
	int lines_removed = 0;
	if (!did_substring_expand) {
		filters.push_back(std::make_shared<meson_test_filter>());
		output = apply_output_filters(cmd, output, filters, &lines_removed);

		if (lines_removed > 0 && ctx.active_agent) {
			ctx.active_agent->increment_stat("test_lines_pruned", lines_removed);
		}
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