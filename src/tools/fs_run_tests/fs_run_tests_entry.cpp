#include <algorithm>
#include <cctype>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
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
		return false;
	}
	auto to_lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
	std::string hay(haystack);
	std::string ned(needle);
	std::transform(hay.begin(), hay.end(), hay.begin(), to_lower);
	std::transform(ned.begin(), ned.end(), ned.begin(), to_lower);
	return hay.find(ned) != std::string::npos;
}

// Tokenizes a test name or query into normalized lowercase alphanumeric tokens,
// filtering out common boilerplate noise tokens like "test" and "turbostar".
static std::vector<std::string> extract_test_tokens(std::string_view s)
{
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : s) {
		if (std::isalnum(static_cast<unsigned char>(c))) {
			cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		} else if (!cur.empty()) {
			if (cur != "test" && cur != "tests" && cur != "turbostar") {
				tokens.push_back(cur);
			}
			cur.clear();
		}
	}
	if (!cur.empty() && cur != "test" && cur != "tests" && cur != "turbostar") {
		tokens.push_back(cur);
	}
	return tokens;
}

// Checks if all significant query tokens are present in the candidate test's token set.
static bool tokens_match(std::span<const std::string> query_tokens,
			 const std::unordered_set<std::string> &cand_tokens)
{
	if (query_tokens.empty()) {
		return false;
	}
	for (const auto &tok : query_tokens) {
		if (!cand_tokens.contains(tok)) {
			return false;
		}
	}
	return true;
}

// Resolves requested test names against available tests with 2-tier matching:
// 1. Exact and literal substring matching.
// 2. Tokenized matching (ignoring noise words 'test', 'tests', 'turbostar').
//    - If exactly 1 candidate matches: auto-runs with a note.
//    - If 2-5 candidates match: surfaces suggestions.
fs_run_tests_tool::resolve_result fs_run_tests_tool::resolve_test_names_detailed(
    std::span<const std::string> test_names, const std::vector<std::string> &available)
{
	resolve_result result;

	for (const auto &t : test_names) {
		if (t.empty()) {
			continue;
		}

		// Tier 1: Exact full-name match
		bool exact_matched = false;
		for (const auto &candidate : available) {
			if (candidate == t) {
				if (std::find(result.resolved_names.begin(), result.resolved_names.end(), t) == result.resolved_names.end()) {
					result.resolved_names.push_back(t);
				}
				exact_matched = true;
				break;
			}
		}
		if (exact_matched) {
			continue;
		}

		// Tier 1b: Literal case-insensitive substring match
		std::vector<std::string> substr_matches;
		for (const auto &candidate : available) {
			if (contains_case_insensitive(candidate, t)) {
				substr_matches.push_back(candidate);
			}
		}
		if (!substr_matches.empty()) {
			for (const auto &cand : substr_matches) {
				if (std::find(result.resolved_names.begin(), result.resolved_names.end(), cand) == result.resolved_names.end()) {
					result.resolved_names.push_back(cand);
				}
			}
			result.did_substring_expand = true;
			continue;
		}

		// Tier 2: Tokenized matching
		auto query_tokens = extract_test_tokens(t);
		std::vector<std::string> token_matches;
		if (!query_tokens.empty()) {
			for (const auto &candidate : available) {
				auto cand_toks = extract_test_tokens(candidate);
				std::unordered_set<std::string> cand_set(cand_toks.begin(), cand_toks.end());
				if (tokens_match(query_tokens, cand_set)) {
					token_matches.push_back(candidate);
				}
			}
		}

		if (token_matches.size() == 1) {
			// Exactly 1 high-confidence match: auto-run it with an informative note
			const std::string &matched_name = token_matches.front();
			if (std::find(result.resolved_names.begin(), result.resolved_names.end(), matched_name) == result.resolved_names.end()) {
				result.resolved_names.push_back(matched_name);
			}
			result.auto_matched_notes.push_back("Note: '" + t + "' matched '" + matched_name + "'.");
		} else if (token_matches.size() > 1 && token_matches.size() <= 5) {
			// Multiple candidates: save as suggestions rather than guessing
			for (const auto &cand : token_matches) {
				if (std::find(result.suggestions.begin(), result.suggestions.end(), cand) == result.suggestions.end()) {
					result.suggestions.push_back(cand);
				}
			}
			result.unresolved_queries.push_back(t);
		} else {
			result.unresolved_queries.push_back(t);
		}
	}

	return result;
}

static std::string escape_ctest_regex(const std::string &name)
{
	static const std::set<char> meta = {'^', '$', '.', '|', '?', '*', '+', '(', ')', '[', ']', '{', '}', '\\'};
	std::string out;
	for (char c : name) {
		if (meta.contains(c)) out += '\\';
		out += c;
	}
	return out;
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
	std::filesystem::path build_path = project_manager::get_instance().resolve_build_dir();

	// Resolve each requested test name against the project's available-test list using
	// exact match, substring match, and tokenized fuzzy matching.
	resolve_result res;
	if (!test_names_.empty()) {
		auto available = project_manager::get_instance().get_available_tests();
		res = resolve_test_names_detailed(test_names_, available);
		// If nothing resolved, the in-memory test list may be stale (e.g. tests were added to
		// meson.build after the editor populated the cache). Invalidate the cache so the next
		// get_available_tests() forces a real refresh, then retry the resolution once before
		// giving up. This makes newly-registered tests discoverable without an editor restart.
		if (res.resolved_names.empty() && res.suggestions.empty()) {
			project_manager::get_instance().invalidate_available_tests_cache();
			available = project_manager::get_instance().get_available_tests();
			res = resolve_test_names_detailed(test_names_, available);
		}
	}
	const auto &resolved = res.resolved_names;
	bool did_substring_expand = res.did_substring_expand;

	std::string cmd;

	if (build_system == "meson") {
		cmd = "CCACHE_DISABLE=1 MESON_TESTTHREADS=2 meson test -C " + build_path.string();

		for (const auto &t : resolved) {
			cmd += " " + fs_utils::escape_shell_arg(t);
		}
	} else if (build_system == "cmake") {
		cmd = "ctest --test-dir " + build_path.string();
		if (!resolved.empty()) {
			cmd += " -R \"(";
			for (size_t i = 0; i < resolved.size(); ++i) {
				cmd += escape_ctest_regex(resolved[i]);
				if (i < resolved.size() - 1)
					cmd += "|";
			}
			cmd += ")\"";
		}
	} else if (build_system == "make") {
		cmd = "make test -C " + build_path.string();
		// Make doesn't have a standard way to run individual tests via 'make test'
	} else {
		cmd = build_system + " test " + build_path.string(); // Fallback
	}

	// If the agent passed specific test names but none resolved, surface suggestions if
	// candidate tests were found, or a pointer to the testlist discovery file.
	if (!test_names_.empty() && resolved.empty()) {
		if (!res.suggestions.empty()) {
			std::string msg;
			if (res.unresolved_queries.size() == 1) {
				msg = std::format("No exact test matched '{}'. Did you mean one of these?\n", res.unresolved_queries.front());
			} else {
				msg = "No exact test matched the requested name(s). Did you mean one of these?\n";
			}
			for (const auto &s : res.suggestions) {
				msg += std::format("  - {}\n", s);
			}
			msg += "Read system://project/testlist.md to discover all valid test names.\n";
			return msg;
		}

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
		output += crashdump_manager::format_crash_notification(crashes_after - crashes_before);
	}

	// Cap output at 10,000 characters to protect context window
	if (output.length() > 10000) {
		output = output.substr(output.length() - 10000);
		output = "\n...[output truncated due to length]...\n" + output;
	}

	std::string notes_prefix;
	for (const auto &note : res.auto_matched_notes) {
		notes_prefix += note + "\n";
	}
	if (!notes_prefix.empty()) {
		notes_prefix += "\n";
	}

	return fs_utils::wrap_prompt_untrusted_data_tag(
	    "fs_run_tests_result", notes_prefix + "```bash\n$ " + cmd + "\n" + output + "\n```");
}

} // namespace tools