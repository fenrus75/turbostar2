#include "resolve_code_review_item.h"
#include "../../agentlib/ai_agent.h"
#include "../../codereview_manager.h"
#include <format>
#include <nlohmann/json.hpp>

namespace tools {

resolve_code_review_item_tool::resolve_code_review_item_tool(resolve_code_review_item_args args)
    : agentlib::llm_tool_action("Resolving code review item " + std::to_string(args.id)), args_(std::move(args))
{
	if (interaction_) {
		interaction_->set_boxed(true, 5, std::to_string(args_.id));
	}
}

bool resolve_code_review_item_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string resolve_code_review_item_tool::execute(agentlib::tool_context& ctx)
{
	// 1. Attempt to resolve the state in the manager
	bool success = codereview_manager::get_instance().resolve_code_review_item(args_.id, args_.commit_hash);

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
			std::string parent_msg = std::format("Subagent resolved code review item (ID: {}) in commit {}.", args_.id, args_.commit_hash);
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
		{"status", "resolved"}
	};
	return response_json.dump(2);
}

} // namespace tools
