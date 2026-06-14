#include "get_code_review_item.h"
#include "../../agentlib/ai_agent.h"
#include "../../codereview_manager.h"
#include <format>
#include <nlohmann/json.hpp>

namespace tools {

get_code_review_item_tool::get_code_review_item_tool(get_code_review_item_args args)
    : agentlib::llm_tool_action("Retrieving code review item " + std::to_string(args.id)), args_(std::move(args))
{
}

bool get_code_review_item_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string get_code_review_item_tool::execute(agentlib::tool_context& ctx)
{
	auto item_opt = codereview_manager::get_instance().get_code_review_item(args_.id);

	if (!item_opt.has_value()) {
		set_failure(ctx, std::format("Error: Code review item with ID {} not found.", args_.id));
		nlohmann::json err_json = {
			{"status", "error"},
			{"message", std::format("Code review item with ID {} not found.", args_.id)}
		};
		return err_json.dump(2);
	}

	const auto& item = item_opt.value();

	// Enforce visibility restriction: only verifiers can see resolved/verified-fixed items
	if (ctx.active_agent) {
		if (ctx.active_agent->get_role() != agentlib::agent_role::verifier) {
			if (item.state == "resolved" || item.state == "verified-fixed") {
				set_failure(ctx, std::format("Error: Code review item {} is resolved/verified and restricted to verifiers.", args_.id));
				nlohmann::json err_json = {
					{"status", "error"},
					{"message", std::format("Access denied. Item {} is resolved/verified and restricted to the verifier role.", args_.id)}
				};
				return err_json.dump(2);
			}
		}
	}

	set_success(ctx);
	nlohmann::json item_j = item;
	return item_j.dump(2);
}

} // namespace tools
