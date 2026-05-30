#include "agentlib/ai_agent.h"
#include "agentlib/interactions/llm_response.h"
#include "project_manager.h"
#include "agent_create.h"
#include <format>
#include <sstream>

namespace tools
{

agent_create_tool::agent_create_tool(agent_create_args args) : args_(std::move(args))
{
}

bool agent_create_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (ctx.active_agent->is_read_only()) {
		out_error = "Execution Error: Agent is in read-only mode and cannot spawn subagents.";
		return false;
	}
	if (args_.name.empty()) {
		out_error = "Execution Error: Subagent name cannot be empty.";
		return false;
	}
	if (args_.profile.empty() && args_.task.empty()) {
		out_error = "Execution Error: You must provide either a 'profile' or a 'task' for the subagent.";
		return false;
	}
	return true;
}

std::string agent_create_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context available.";
	}
	if (ctx.active_agent->is_read_only()) {
		return "Error: Agent is in read-only mode.";
	}

	auto new_agent = ctx.active_agent->spawn_subagent(args_.name);
	if (!new_agent) {
		return "Error: Failed to create subagent.";
	}

	// Always inject base project knowledge into subagents
	new_agent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());

	if (!args_.profile.empty() && !args_.task.empty()) {
		new_agent->inject_context("system", args_.profile);
		new_agent->submit_prompt(args_.task);
	} else if (!args_.profile.empty()) {
		new_agent->submit_prompt(args_.profile);
	} else if (!args_.task.empty()) {
		new_agent->submit_prompt(args_.task);
	}

	if (!args_.wait) {
		return std::format("Agent '{}' created successfully with ID: {}. Agent started asynchronously. Use wait_for_agent({}) to wait for the agent to finish.",
		                   args_.name, new_agent->get_id(), new_agent->get_id());
	}

	// Synchronous execution: save original status and restore it afterwards
	auto old_status = ctx.active_agent->get_status();
	ctx.active_agent->set_status(agentlib::agent_status::waiting, new_agent->get_id());
	new_agent->wait_until_idle();
	ctx.active_agent->set_status(old_status);

	if (new_agent->get_status() == agentlib::agent_status::error) {
		return std::format("Agent '{}' encountered an error during execution.", args_.name);
	}

	// Retrieve interactions and find the last LLM response
	const auto &interactions = new_agent->get_interactions();
	for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
		auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
		if (res) {
			return res->get_text();
		}
	}

	return std::format("Agent '{}' completed successfully, but no response text was found.", args_.name);
}

} // namespace tools