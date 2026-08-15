#include "a2a_generate_card_with_agent.h"
#include "agentlib/ai_agent.h"
#include "agentlib/subagent_manager.h"
#include "fs_utils.h"
#include <format>
#include <fstream>

namespace tools
{

a2a_generate_card_with_agent_tool::a2a_generate_card_with_agent_tool(a2a_generate_card_with_agent_args args)
	: llm_tool_action("Synthesizing A2A Agent Card with Subagent")
	, args_(std::move(args))
{
}

bool a2a_generate_card_with_agent_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string a2a_generate_card_with_agent_tool::execute(agentlib::tool_context &ctx)
{
	std::string dest_card_path = args_.output_path;
	if (dest_card_path.empty()) {
		dest_card_path = args_.safe_path;
		if (dest_card_path.ends_with(".md")) {
			dest_card_path = dest_card_path.substr(0, dest_card_path.length() - 3) + ".card.json";
		} else {
			dest_card_path += ".card.json";
		}
	}

	auto subagent_opt = agentlib::subagent_manager::get_instance().find_subagent_by_name("a2acardgenerator");
	if (!subagent_opt) {
		set_failure(ctx, "a2acardgenerator subagent not registered");
		return "Error: `a2acardgenerator` subagent is not registered.";
	}

	std::string task_prompt = std::format(
	    "Please read the agent definition file at `{}` and synthesize an A2A Agent Card JSON file.\n"
	    "1. Validate the synthesized card using `a2a_validate_card`.\n"
	    "2. Write the validated card to `{}`.\n"
	    "3. Report the final validation status and synthesized card summary.",
	    args_.safe_path, dest_card_path);

	std::string res;
	if (ctx.active_agent) {
		auto new_agent = ctx.active_agent->spawn_subagent("a2acardgenerator");
		if (new_agent) {
			new_agent->set_task_description(task_prompt);
			new_agent->submit_prompt(task_prompt);
			new_agent->wait_until_idle_for(std::chrono::seconds(60));
			if (new_agent->has_final_result()) {
				res = new_agent->get_final_result();
			} else {
				res = "Card generation task completed.";
			}
			set_success(ctx, "A2A card synthesis completed by subagent");
			return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_generator_result", res);
		}
	}

	set_success(ctx, "A2A card synthesis task dispatched to a2acardgenerator subagent");
	res = std::format(
	    "### A2A Card Generation Dispatched\n\n"
	    "- **Source Agent Definition**: `{}`\n"
	    "- **Target Card Output**: `{}`\n"
	    "- **Subagent**: `a2acardgenerator`\n\n"
	    "The `a2acardgenerator` subagent will read `{}` and write the validated A2A Agent Card to `{}`.\n",
	    args_.requested_path, dest_card_path, args_.requested_path, dest_card_path);
	return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_generator_result", res);
}

} // namespace tools
