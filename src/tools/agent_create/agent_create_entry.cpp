#include <sstream>
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/llm_response.h"
#include "../../project_manager.h"
#include "agent_create.h"

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
	return true;
}

std::string agent_create_tool::execute(agentlib::tool_context &ctx)
{
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
		return "Agent '" + args_.name + "' created successfully with ID: " + std::to_string(new_agent->get_id()) +
		       ". Agent started asynchronously. Use wait_for_agent(" + std::to_string(new_agent->get_id()) +
		       ") to wait for the agent to finish.";
	}

	// Synchronous execution
	ctx.active_agent->set_status(agentlib::agent_status::waiting, new_agent->get_id());
	new_agent->wait_until_idle();
	ctx.active_agent->set_status(agentlib::agent_status::tool_execution);

	if (new_agent->get_status() == agentlib::agent_status::error) {
		return "Agent '" + args_.name + "' encountered an error during execution.";
	}

	// Retrieve interactions and find the last LLM response
	const auto &interactions = new_agent->get_interactions();
	for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
		auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
		if (res) {
			return res->get_text();
		}
	}

	return "Agent '" + args_.name + "' completed successfully, but no response text was found.";
}

} // namespace tools