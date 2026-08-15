#include "get_subagent_output.h"
#include "agentlib/ai_agent.h"
#include "fs_utils.h"
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace tools {

get_subagent_output_tool::get_subagent_output_tool(get_subagent_output_args args) : args_(std::move(args)) {}

bool get_subagent_output_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (ctx.active_agent->is_read_only() && !args_.keep) {
		out_error = "Execution Error: Agent is in read-only mode and cannot terminate subagents. Run with 'keep': true.";
		return false;
	}
	return true;
}

std::string get_subagent_output_tool::execute(agentlib::tool_context &ctx)
{
	auto subagents = ctx.active_agent->get_subagents();

	std::shared_ptr<agentlib::ai_agent> target_agent = nullptr;
	for (const auto &sub : subagents) {
		if (sub->get_id() == args_.id) {
			target_agent = sub;
			break;
		}
	}

	if (!target_agent) {
		return std::format("Error: Could not find subagent with ID {}", args_.id);
	}

	if (target_agent->has_final_result()) {
		std::string final_res = target_agent->get_final_result();
		if (!args_.keep) {
			target_agent->close();
			ctx.active_agent->remove_subagent(target_agent->get_id());
		}
		return fs_utils::wrap_prompt_untrusted_data_tag("subagent_output", final_res);
	}

	auto interactions = target_agent->get_interactions();
	if (interactions.empty()) {
		return std::format("Subagent ID {} has no interaction history.", args_.id);
	}

	std::string history_text;
	size_t accumulated = 0;
	for (const auto &interaction : interactions) {
		std::string raw = interaction->get_raw_text();
		if (accumulated + raw.size() > 32768) {
			history_text += raw.substr(0, 32768 - accumulated) + "\n... (history truncated)\n";
			break;
		}
		history_text += raw + "\n\n";
		accumulated += raw.size() + 2;
	}

	std::string response;
	if (!args_.keep) {
		target_agent->close();
		ctx.active_agent->remove_subagent(target_agent->get_id());
		response = std::format(
			"Interaction history for Subagent ID {} ({}):\n\n"
			"{}--- End of History ---\n"
			"Subagent {} has been terminated and resources freed.",
			target_agent->get_id(), target_agent->get_name(), history_text, args_.id);
	} else {
		response = std::format(
			"Interaction history for Subagent ID {} ({}):\n\n"
			"{}--- End of History ---\n"
			"If you are finished with this subagent, you should use the kill_subagent({}) tool to clean it up and free resources. "
			"Alternatively, you can send it new instructions using the send_message({}, \"<message>\") tool.",
			target_agent->get_id(), target_agent->get_name(), history_text, target_agent->get_id(), target_agent->get_id());
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("subagent_output", response);
}

} // namespace tools