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

static void run_verifier_async_thread(std::vector<std::shared_ptr<agentlib::ai_agent>> reviewer_agents, std::shared_ptr<agentlib::ai_agent> parent,
				      perform_code_review_args args)
{
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
			return "Error: Failed to create code reviewer agent.";
		}
		reviewer_agent->set_role(agentlib::agent_role::reviewer);
		reviewer_agent->set_model(reviewer_model);

		if (!args_.result_file.empty()) {
			reviewer_agent->set_allowed_write_file(args_.result_file);
		}

		// Compile previous code reviews for the target files in this group
		auto all_items = codereview_manager::get_instance().list_code_review_items("", "", true);
		std::vector<review_item> relevant_items;
		for (const auto &item : all_items) {
			for (const auto &f : group_files) {
				if (item.filename.starts_with(f)) {
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

		// Build todo list instructions
		std::string todos_section;
		if (!args_.todos.empty()) {
			todos_section = "You are explicitly asked to address the following todo items and mark them complete using the "
					"`agent_complete_todo` tool once completed:\n"
					"| Todo Item |\n|---|\n";
			for (const auto &t : args_.todos) {
				todos_section += std::format("| {} |\n", t);
				reviewer_agent->add_todo(t);
			}
		} else {
			todos_section = "No specific todo items assigned.";
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
				"### assigned Tasks:\n"
				"{}\n\n"
				"### Final Output Guidelines:\n"
				"Once and only once you have created/updated all code review items for all identified issues using the tools:\n"
				"1. Write a markdown summary of your findings to the designated report file if one is configured: `{}`.\n"
				"2. Report your final results back to the parent agent using the `agent_report_final_result` tool.",
				files_list_str, reviews_section, todos_section, args_.result_file);

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
		// Asynchronous: spawn all reviewers and the verifier in the background and return immediately
		std::thread(run_verifier_async_thread, reviewer_agents, parent, args_).detach();
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

	// Spawn Agent 2 (Verifier) asynchronously in the background as a sanity check
	std::thread(run_verifier_async_thread, reviewer_agents, parent, args_).detach();

	set_success(ctx);

	if (reviewer_agents.size() == 1) {
		auto reviewer_agent = reviewer_agents[0];
		if (reviewer_agent->get_status() == agentlib::agent_status::error) {
			return std::format("Code Reviewer Agent '{}' encountered an error during execution.", reviewer_agent->get_name());
		}

		if (reviewer_agent->has_final_result()) {
			return reviewer_agent->get_final_result();
		}

		const auto &interactions = reviewer_agent->get_interactions();
		for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
			auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
			if (res) {
				return res->get_text();
			}
		}

		return std::format("Code Reviewer Agent '{}' completed successfully, but no final response was found.", reviewer_agent->get_name());
	}

	std::string combined_result;
	for (const auto &reviewer_agent : reviewer_agents) {
		if (!combined_result.empty()) {
			combined_result += "\n\n---\n\n";
		}
		combined_result += std::format("### Reviewer Agent '{}' Result:\n", reviewer_agent->get_name());
		if (reviewer_agent->get_status() == agentlib::agent_status::error) {
			combined_result += "Encountered an error during execution.";
			continue;
		}
		if (reviewer_agent->has_final_result()) {
			combined_result += reviewer_agent->get_final_result();
			continue;
		}
		bool found_resp = false;
		const auto &interactions = reviewer_agent->get_interactions();
		for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
			auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
			if (res) {
				combined_result += res->get_text();
				found_resp = true;
				break;
			}
		}
		if (!found_resp) {
			combined_result += "Completed successfully, but no final response was found.";
		}
	}

	return combined_result;
}

} // namespace tools
