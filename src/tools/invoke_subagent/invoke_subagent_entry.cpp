#include "a2a/a2a_client.h"
#include "a2a/a2a_server_manager.h"
#include "agentlib/ai_agent.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/interactions/llm_response.h"
#include "fs_utils.h"
#include "git_manager.h"
#include "project_manager.h"
#include "invoke_subagent.h"
#include <format>
#include <sstream>

namespace tools
{

invoke_subagent_tool::invoke_subagent_tool(invoke_subagent_args args) : args_(std::move(args))
{
}

bool invoke_subagent_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string invoke_subagent_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context available.";
	}

	std::string target_name = !args_.subagent_name.empty() ? args_.subagent_name : args_.name;
	auto colon_pos = target_name.find(':');
	if (colon_pos != std::string::npos) {
		if (args_.local_only) {
			return "Error: Cannot invoke remote subagent '" + target_name + "' when local_only is true.";
		}

		std::string server_name = target_name.substr(0, colon_pos);
		std::string remote_agent = target_name.substr(colon_pos + 1);

		auto server_cfg = a2a::a2a_server_manager::get_instance().find_server(server_name);
		if (!server_cfg.has_value()) {
			return std::format("Error: A2A Server '{}' not found in server registry.", server_name);
		}

		std::string prompt = args_.task;
		if (!args_.profile.empty()) {
			prompt = std::format("{}\n\n{}", args_.profile, prompt);
		}

		std::string repo_url = args_.repository_url;
		std::string git_ref = args_.git_ref;
		if (repo_url.empty()) {
			std::string origin_url = git_manager::get_instance().get_remote_origin_url();
			if (!origin_url.empty() && (origin_url.find("http") != std::string::npos || origin_url.find("git") != std::string::npos)) {
				repo_url = origin_url;
			}
			if (git_ref.empty()) {
				git_ref = git_manager::get_instance().get_current_branch();
			}
		}

		if (args_.wait) {
			auto res = a2a::a2a_client::get_instance().execute_task_sync(server_cfg->url, remote_agent, prompt, 60, server_cfg->auth_token, repo_url, git_ref);
			if (res.success) {
				return res.output_text;
			}
			return std::format("Error executing task on remote A2A server '{}:{}': {}", server_name, remote_agent, res.error_message);
		} else {
			auto res = a2a::a2a_client::get_instance().submit_task(server_cfg->url, remote_agent, prompt, server_cfg->auth_token, repo_url, git_ref);
			if (res.success) {
				return std::format("Remote subagent '{}:{}' invoked successfully with Task ID: {}. Task running asynchronously on A2A server '{}'.",
				                   server_name, remote_agent, res.task_id, server_cfg->url);
			}
			return std::format("Error submitting task to remote A2A server '{}:{}': {}", server_name, remote_agent, res.error_message);
		}
	}

	auto new_agent = ctx.active_agent->spawn_subagent(args_.name);
	if (!new_agent) {
		return "Error: Failed to create subagent.";
	}

	new_agent->set_task_description(args_.task);

	// Always inject base project knowledge into subagents
	new_agent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());
	new_agent->inject_context("system", "Instructions for subagent: When you have completed your task, you MUST call the `report_final_result` tool to report your final findings back to the parent agent. This ensures that the parent agent receives only your final response rather than your entire conversation history.");

	if (!args_.subagent_name.empty()) {
		auto sa = agentlib::subagent_manager::get_instance().find_subagent_by_name(args_.subagent_name);
		if (sa) {
			agentlib::agent_properties props = new_agent->get_properties();
			props.read_only = sa->read_only || ctx.active_agent->is_read_only();
			if (!sa->tool_families.empty()) {
				props.active_families = sa->tool_families;
			}
			new_agent->set_properties(props);

			if (!sa->animation_name.empty()) {
				new_agent->set_animation_name(sa->animation_name);
			}

			if (!sa->system_prompt.empty()) {
				new_agent->inject_context("system", sa->system_prompt);
			}
		}
	}

	if (ctx.active_agent->is_read_only()) {
		agentlib::agent_properties props = new_agent->get_properties();
		props.read_only = true;
		new_agent->set_properties(props);
	}

	if (!args_.profile.empty()) {
		new_agent->inject_context("system", args_.profile);
	}
	if (!args_.task.empty()) {
		new_agent->submit_prompt(args_.task);
	}

	if (!args_.wait) {
		return std::format("Subagent '{}' invoked successfully with ID: {}. Subagent started asynchronously. Use wait_for_subagent({}) to wait for the subagent to finish.",
		                   args_.name, new_agent->get_id(), new_agent->get_id());
	}

	// Synchronous execution: save original status and restore it afterwards
	auto old_status = ctx.active_agent->get_status();
	ctx.active_agent->set_status(agentlib::agent_status::waiting, new_agent->get_id());
	new_agent->set_notify_parent_on_completion(false);
	bool completed = new_agent->wait_until_idle_for(std::chrono::seconds(120));
	ctx.active_agent->set_status(old_status);

	if (!completed) {
		return "Error: Timed out waiting for subagent '" + args_.name + "' to complete task (120s limit exceeded).";
	}

	if (new_agent->get_status() == agentlib::agent_status::error) {
		return std::format("Subagent '{}' encountered an error during execution.", args_.name);
	}

	if (new_agent->has_final_result()) {
		return fs_utils::wrap_prompt_untrusted_data_tag("subagent_result", new_agent->get_final_result());
	}

	// Retrieve interactions and find the last LLM response
	const auto &interactions = new_agent->get_interactions();
	for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
		auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
		if (res) {
			return fs_utils::wrap_prompt_untrusted_data_tag("subagent_result", res->get_text());
		}
	}

	return std::format("Subagent '{}' completed successfully, but no response text was found.", args_.name);
}

} // namespace tools