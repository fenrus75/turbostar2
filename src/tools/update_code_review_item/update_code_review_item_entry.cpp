#include "update_code_review_item.h"
#include "../../agentlib/ai_agent.h"
#include "../../codereview_manager.h"
#include <format>
#include <nlohmann/json.hpp>

namespace tools {

update_code_review_item_tool::update_code_review_item_tool(update_code_review_item_args args)
    : agentlib::llm_tool_action("Updating code review item " + std::to_string(args.id)), args_(std::move(args))
{
	if (interaction_) {
		interaction_->set_boxed(true, 5, std::to_string(args_.id));
	}
}

bool update_code_review_item_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string update_code_review_item_tool::execute(agentlib::tool_context& ctx)
{
	// 1. Perform the update in the manager
	bool success = codereview_manager::get_instance().update_code_review_item(
	    args_.id,
	    args_.state,
	    args_.severity,
	    args_.description,
	    args_.proposed_fix
	);

	if (!success) {
		set_failure(ctx, std::format("Error: Code review item with ID {} not found.", args_.id));
		nlohmann::json err_json = {
			{"id", args_.id},
			{"status", "error"},
			{"message", std::format("Code review item with ID {} not found.", args_.id)}
		};
		return err_json.dump(2);
	}

	// 2. Thread-safe parent agent notification injection
	if (ctx.active_agent) {
		auto parent = ctx.active_agent->get_parent();
		if (parent) {
			std::string parent_msg = std::format("Subagent updated code review item (ID: {}).", args_.id);
			parent->inject_context("user", parent_msg, false);
		}
	}

	// 3. Broadcast the codereview_updated event to the global event queue
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::codereview_updated;
		ev.key_code = args_.id;
		ctx.queue->push(ev);
	}

	set_success(ctx);

	nlohmann::json response_json = {
		{"id", args_.id},
		{"status", "updated"}
	};
	return response_json.dump(2);
}

} // namespace tools
