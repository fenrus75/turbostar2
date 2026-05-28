#include <chrono>
#include <thread>
#include "../../agentlib/ai_agent.h"
#include "agent_set_timer.h"
#include "../../event_logger.h"

namespace tools
{

agent_set_timer_tool::agent_set_timer_tool(agent_set_timer_args args) : args_(std::move(args))
{
}

bool agent_set_timer_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (args_.seconds <= 0) {
		out_error = "Execution Error: Seconds must be a positive integer.";
		return false;
	}
	return true;
}

std::string agent_set_timer_tool::execute(agentlib::tool_context &ctx)
{
	// Detach a background thread to wait and then trigger
	std::thread([agent = ctx.active_agent->shared_from_this(), seconds = args_.seconds]() {
		event_logger::get_instance().log("Timer thread started: sleeping for " + std::to_string(seconds) + " seconds.");
		std::this_thread::sleep_for(std::chrono::seconds(seconds));

		// When the timer triggers, IF the agent is idle, we inject the context.
		if (agent->get_status() == agentlib::agent_status::idle) {
			event_logger::get_instance().log("Timer expired: Agent is idle, injecting expiration message.");
			agent->inject_context("system", "previously set timer expired", true);
		} else {
			event_logger::get_instance().log("Timer expired: Agent is not idle (status = " +
				std::to_string(static_cast<int>(agent->get_status())) + "), skipping injection.");
		}
	}).detach();

	return "Timer set for " + std::to_string(args_.seconds) + " seconds.";
}

} // namespace tools
