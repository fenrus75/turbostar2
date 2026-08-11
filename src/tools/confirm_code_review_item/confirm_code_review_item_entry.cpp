#include "confirm_code_review_item.h"
#include "../../agentlib/ai_agent.h"
#include "../../codereview_manager.h"
#include <format>
#include <nlohmann/json.hpp>

namespace tools {

confirm_code_review_item_tool::confirm_code_review_item_tool(confirm_code_review_item_args args)
    : agentlib::llm_tool_action("Confirming code review item " + std::to_string(args.id)), args_(std::move(args))
{
	if (interaction_) {
		interaction_->set_boxed(true, 5, std::to_string(args_.id));
	}
}

bool confirm_code_review_item_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string confirm_code_review_item_tool::execute(agentlib::tool_context& ctx)
{
	// 1. Attempt to confirm/verify the state transition in the manager
	bool success = codereview_manager::get_instance().confirm_code_review_item(args_.id);

	if (!success) {
		set_failure(ctx, std::format("Error: Code review item with ID {} not found, or is not in a states allowed for confirmation/verification.", args_.id));
		// Prefix with "Error: " so the agent run-loop classifies this as a failed tool result
		// (is_error heuristic in ai_agent.cpp).
		nlohmann::json err_json = {
			{"id", args_.id},
			{"status", "error"},
			{"message", std::format("Code review item with ID {} not found or has invalid state for confirmation.", args_.id)}
		};
		return "Error: " + err_json.dump(2);
	}

	// 2. Broadcast the codereview_updated event to the global event queue.
	// NOTE: Deliberately no parent-context injection here. update/confirm/resolve are
	// bookkeeping; per-item notifications were found to be noisy and redundant (the agent
	// already gets results via toolcall returns / report_final_result).
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::codereview_updated;
		ev.key_code = args_.id;
		ctx.queue->push(ev);
	}

	set_success(ctx);

	nlohmann::json response_json = {
		{"id", args_.id},
		{"status", "confirmed"}
	};
	return response_json.dump(2);
}

} // namespace tools
