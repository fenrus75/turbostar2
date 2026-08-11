#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/llm_response.h"
#include "../../codereview_manager.h"
#include "../../config_manager.h"
#include "../../event_logger.h"
#include "../../fs_utils.h"
#include "../../project_manager.h"
#include "perform_code_review.h"

namespace tools
{

// Boundary-aware file scoping: an item belongs to a reviewed file if its stored filename is
// exactly the file, or lives under it as a directory prefix. A naive prefix match (starts_with)
// wrongly attributes e.g. "src/foobar.h" to a review of "src/foo". Returns true if `item_file`
// should be considered part of the reviewed `target_file`.
static bool item_file_in_scope(const std::string &item_file, const std::string &target_file)
{
	if (item_file == target_file) {
		return true;
	}
	return item_file.size() > target_file.size() && item_file.starts_with(target_file) &&
	       item_file[target_file.size()] == '/';
}

static void run_verifier_async_thread(std::vector<std::weak_ptr<agentlib::ai_agent>> reviewer_agents_weak,
				      std::weak_ptr<agentlib::ai_agent> parent_weak, perform_code_review_args args)
{
	// Lock the agents - they may have been destroyed by the time this thread runs if the
	// caller/editor shut down in the meantime. Detached threads MUST NOT keep parent/context
	// objects alive via strong shared_ptr (docs/thread-lifecycle.md Pattern C).
	std::vector<std::shared_ptr<agentlib::ai_agent>> reviewer_agents;
	for (const auto &w : reviewer_agents_weak) {
		if (auto a = w.lock()) {
			reviewer_agents.push_back(std::move(a));
		}
	}
	auto parent = parent_weak.lock();
	if (!parent || reviewer_agents.empty()) {
		return;
	}

	// Wait for all reviewer agents to finish
	for (auto &agent : reviewer_agents) {
		agent->wait_until_idle();
	}

	bool all_failed = true;
	for (const auto &agent : reviewer_agents) {
		if (agent->get_status() != agentlib::agent_status::error) {
			all_failed = false;
		} else {
			event_logger::get_instance().log(std::format("Reviewer agent '{}' failed.", agent->get_name()));
		}
	}

	if (all_failed && !reviewer_agents.empty()) {
		event_logger::get_instance().log("All reviewer agents failed, skipping verifier agent.");
		return;
	}

	// Resolve the model configured for verification
	std::string verifier_model_id = config_manager::get_instance().get_task_model_id("code_verifier");
	auto verifier_model = agentlib::ai_model_registry::get_instance().get_model(verifier_model_id);
	if (!verifier_model) {
		verifier_model = agentlib::ai_model_registry::get_instance().get_default_model();
	}
	if (!verifier_model) {
		verifier_model = parent->get_model();
	}

	auto verifier_agent = parent->spawn_subagent("Review Verifier");
	if (!verifier_agent) {
		return;
	}
	verifier_agent->set_role(agentlib::agent_role::verifier);
	verifier_agent->set_model(verifier_model);
	// The verifier uses confirm_code_review_item / list_code_review_items (code_review family);
	// grant the family explicitly so availability does not depend on transient global state.
	verifier_agent->add_active_tool_family("code_review");
	// The verifier reports through report_final_result and its updates are bookkeeping;
	// suppress noisy per-item parent injections (create stays terse but falls under this too
	// for the background verifier).
	verifier_agent->set_suppress_parent_injection(true);

	std::string system_prompt =
	    "You are a code review verification agent. Your task is to verify the code review findings reported by the reviewer agent.\n"
	    "Inspect the files and verify if the reported issues are correct, transitioning them from 'new' to 'confirmed' or 'disputed'.\n"
	    "Also, if the developer claims to have resolved an issue, verify if the fix is indeed correct and transition it to "
	    "'verified-fixed'.\n"
	    "Use the confirm_code_review_item tool to confirm/verify items, and list_code_review_items to retrieve the list of items.\n";

	verifier_agent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());
	verifier_agent->inject_context("system", system_prompt);
	verifier_agent->inject_context("system", "Instructions for subagent: When you have completed your verification, call the "
						 "`agent_report_final_result` tool to report your final findings.");

	std::string task_prompt = "Perform review verification on the following files:\n";
	for (const auto &f : args.files) {
		task_prompt += "- " + f + "\n";
	}
	verifier_agent->submit_prompt(task_prompt);
}

perform_code_review_tool::perform_code_review_tool(perform_code_review_args args)
    : agentlib::llm_tool_action("Performing code review"), args_(std::move(args))
{
}

bool perform_code_review_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (args_.files.empty()) {
		out_error = "Execution Error: You must specify at least one file to review.";
		return false;
	}
	return true;
}

std::string perform_code_review_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context available.";
	}

	auto parent = ctx.active_agent->shared_from_this();

	// Watermark of the next item ID before we start spawning reviewers. The sync summary table
	// uses this to report ONLY the findings created by this run, not pre-existing/historical
	// items for the same files.
	const int start_item_watermark = codereview_manager::get_instance().get_next_item_id();

	// 1. Resolve reviewer model
	std::string reviewer_model_id = config_manager::get_instance().get_task_model_id("code_reviewer");
	auto reviewer_model = agentlib::ai_model_registry::get_instance().get_model(reviewer_model_id);
	if (!reviewer_model) {
		reviewer_model = agentlib::ai_model_registry::get_instance().get_default_model();
	}
	if (!reviewer_model) {
		reviewer_model = parent->get_model();
	}

	// Group the files: groups of at most 10 files OR cumulative line count of 1500 lines.
	// Scope Constraint: Do NOT split within a file (no intra-file splitting).
	// If a single file itself has >1500 lines, it goes into its own group as-is.
	std::vector<std::vector<std::string>> file_groups;
	std::vector<std::string> current_group;
	int current_file_count = 0;
	int current_line_count = 0;

	std::filesystem::path workspace_root(project_manager::get_instance().get_project_root());

	for (const auto &f : args_.files) {
		std::filesystem::path full_path = workspace_root / f;
		int line_count = 0;
		std::string line_count_str = fs_utils::count_lines_in_file(full_path.string());
		if (!line_count_str.empty()) {
			try {
				line_count = std::stoi(line_count_str);
			} catch (...) {
				line_count = 0;
			}
		}

		if (current_group.empty()) {
			current_group.push_back(f);
			current_file_count = 1;
			current_line_count = line_count;
		} else {
			if (current_file_count + 1 > 10 || current_line_count + line_count > 1500) {
				file_groups.push_back(current_group);
				current_group.clear();
				current_group.push_back(f);
				current_file_count = 1;
				current_line_count = line_count;
			} else {
				current_group.push_back(f);
				current_file_count += 1;
				current_line_count += line_count;
			}
		}
	}
	if (!current_group.empty()) {
		file_groups.push_back(current_group);
	}

	std::vector<std::shared_ptr<agentlib::ai_agent>> reviewer_agents;

	for (size_t i = 0; i < file_groups.size(); ++i) {
		const auto &group_files = file_groups[i];

		std::string agent_name = file_groups.size() > 1 ? std::format("Code Reviewer (Group {})", i + 1) : "Code Reviewer";
		auto reviewer_agent = parent->spawn_subagent(agent_name);
		if (!reviewer_agent) {
			// A spawn failed partway through the groups: cancel/close any reviewers already
			// started this iteration so they don't leak as orphaned background agents.
			for (auto &started : reviewer_agents) {
				started->cancel_current_task();
				started->set_status(agentlib::agent_status::dead);
				parent->remove_subagent(started->get_id());
			}
			return "Error: Failed to create code reviewer agent.";
		}
		reviewer_agent->set_role(agentlib::agent_role::reviewer);
		reviewer_agent->set_model(reviewer_model);
		reviewer_agent->add_active_tool_family("code_review");

		// Findings are delivered via the toolcall result (sync) or report_final_result, so
		// suppress per-item parent injections from the CRUD tools to avoid stream noise.
		reviewer_agent->set_suppress_parent_injection(true);

		if (!args_.result_file.empty()) {
			reviewer_agent->set_allowed_write_file(args_.result_file);
		}

		// Compile previous code reviews for the target files in this group
		auto all_items = codereview_manager::get_instance().list_code_review_items("", "", true);
		std::vector<review_item> relevant_items;
		for (const auto &item : all_items) {
			for (const auto &f : group_files) {
				if (item_file_in_scope(item.filename, f)) {
					relevant_items.push_back(item);
					break;
				}
			}
		}

		std::string reviews_section;
		if (!relevant_items.empty()) {
			reviews_section =
			    "There are existing code review items for the files under review. Part of your task is to verify their correctness and "
			    "relevance:\n"
			    "- Use `update_code_review_item` to adjust details, or to set state to \"invalid\" if the issue no longer applies.\n"
			    "- Use `get_code_review_item` to fetch full descriptions and proposed fixes for any of the following items:\n\n"
			    "| ID | State | File:Line | Summary |\n|---|---|---|---|\n";
			for (const auto &ri : relevant_items) {
				std::string loc = ri.filename;
				if (ri.line_number > 0) {
					loc += std::format(":{}", ri.line_number);
				}
				std::string clean_summary = ri.summary;
				std::replace(clean_summary.begin(), clean_summary.end(), '|', ' ');
				reviews_section += std::format("| {} | {} | {} | {} |\n", ri.id, ri.state, loc, clean_summary);
			}
		} else {
			reviews_section = "No previous code review items exist for these files.\n";
		}

		std::string files_list_str;
		for (const auto &f : group_files) {
			files_list_str += "- " + f + "\n";
		}

		// Build and inject system prompt
		std::string system_prompt =
		    std::format("You are a code review agent. Your task is to perform a detailed code review based on the user request. Do not "
				"attempt to modify any source files to apply fixes; your execution context is read-only.\n"
				"CRITICAL REQUIREMENT: For EVERY issue, bug, or improvement you identify, you MUST first call the `create_code_review_item` "
				"tool to record the finding in the database BEFORE reporting your final summary. Do NOT bypass the "
				"`create_code_review_item` tool to report findings only in the final text response.\n\n"
				"### Files under review:\n{}"
				"\n"
				"### Previous Code Reviews:\n{}"
				"\n"
				"### Final Output Guidelines:\n"
				"Once and only once you have created/updated all code review items for all identified issues using the tools:\n"
				"1. Write a markdown summary of your findings to the designated report file if one is configured: `{}`.\n"
				"2. Report your final results back to the parent agent using the `agent_report_final_result` tool.",
				files_list_str, reviews_section, args_.result_file);

		reviewer_agent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());
		reviewer_agent->inject_context("system", system_prompt);
		std::string task_prompt = "Please review these files:\n" + files_list_str;
		if (!args_.instructions.empty()) {
			task_prompt += "\nInstructions:\n" + args_.instructions;
		}
		reviewer_agent->submit_prompt(task_prompt);

		reviewer_agents.push_back(reviewer_agent);
	}

	if (args_.async) {
		// Asynchronous: spawn all reviewers and the verifier in the background and return immediately.
		// Pass weak refs so the detached thread does not keep the agents alive (Pattern C).
		std::vector<std::weak_ptr<agentlib::ai_agent>> reviewer_agents_weak;
		for (const auto &a : reviewer_agents) {
			reviewer_agents_weak.push_back(a);
		}
		std::thread(run_verifier_async_thread, std::move(reviewer_agents_weak), std::weak_ptr<agentlib::ai_agent>(parent),
			    args_)
		    .detach();
		set_success(ctx);

		std::string id_list;
		for (size_t i = 0; i < reviewer_agents.size(); ++i) {
			if (i > 0) {
				id_list += ", ";
			}
			id_list += std::to_string(reviewer_agents[i]->get_id());
		}
		if (reviewer_agents.size() == 1) {
			return std::format("Code review started asynchronously. Reviewer Agent ID: {}.", id_list);
		}
		return std::format("Code review started asynchronously. Reviewer Agent IDs: {}.", id_list);
	}

	// Synchronous: Wait for all reviewer agents to finish
	auto old_status = parent->get_status();
	for (auto &reviewer_agent : reviewer_agents) {
		parent->set_status(agentlib::agent_status::waiting, reviewer_agent->get_id());
		reviewer_agent->wait_until_idle();
	}
	parent->set_status(old_status);

	// Spawn Agent 2 (Verifier) asynchronously in the background as a sanity check.
	// Pass weak refs so the detached thread does not keep the agents alive (Pattern C).
	std::vector<std::weak_ptr<agentlib::ai_agent>> reviewer_agents_weak;
	for (const auto &a : reviewer_agents) {
		reviewer_agents_weak.push_back(a);
	}
	std::thread(run_verifier_async_thread, std::move(reviewer_agents_weak), std::weak_ptr<agentlib::ai_agent>(parent),
		    args_)
	    .detach();

	set_success(ctx);

	// Gather per-reviewer failure status so we can surface any agent that errored out.
	std::vector<std::string> failed_reviewers;
	for (const auto &reviewer_agent : reviewer_agents) {
		if (reviewer_agent->get_status() == agentlib::agent_status::error) {
			failed_reviewers.push_back(reviewer_agent->get_name());
		}
	}

	// Build a compact summary table of the findings created in THIS run, sourced from the
	// review-item database (the reviewer persists every finding via create_code_review_item).
	// This replaces the noisy per-item parent injections; the caller gets one clean result.
	// The id watermark (captured before spawning reviewers) scopes the table to only the items
	// created by this invocation, excluding pre-existing/historical findings for the same files.
	auto all_items = codereview_manager::get_instance().list_code_review_items("", "", true);
	std::string header = "Code review complete. Findings:\n";
	std::string table = "| ID | severity | file:line | summary |\n|---|---|---|---|\n";
	int shown = 0;
	for (const auto &item : all_items) {
		if (item.id < start_item_watermark) {
			continue; // pre-existing item, not part of this run
		}
		bool in_scope = false;
		for (const auto &f : args_.files) {
			if (item_file_in_scope(item.filename, f)) {
				in_scope = true;
				break;
			}
		}
		if (!in_scope) {
			continue;
		}
		std::string location = item.filename;
		if (item.line_number > 0) {
			location = std::format("{}:{}", item.filename, item.line_number);
		}
		std::string clean_summary = item.summary;
		std::replace(clean_summary.begin(), clean_summary.end(), '|', ' ');
		auto last = clean_summary.find_last_not_of(" \t\r\n");
		if (last != std::string::npos) {
			clean_summary.erase(last + 1);
		}
		table += std::format("| {} | {} | {} | {} |\n", item.id, item.severity, location, clean_summary);
		++shown;
	}

	if (shown == 0) {
		header = "Code review complete. No findings were recorded for the reviewed files.\n";
	}

	if (!failed_reviewers.empty()) {
		std::string errs;
		for (size_t i = 0; i < failed_reviewers.size(); ++i) {
			if (i > 0)
				errs += ", ";
			errs += "'" + failed_reviewers[i] + "'";
		}
		return header + table + "\nNote: reviewer agent(s) " + errs + " encountered an error.";
	}

	return header + table;
}

} // namespace tools
