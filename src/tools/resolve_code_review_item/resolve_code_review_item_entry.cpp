#include "resolve_code_review_item.h"
#include "agentlib/ai_agent.h"
#include "codereview_manager.h"
#include "fs_utils.h"
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
		std::string err_msg = std::format("Error: Code review item with ID {} not found or has invalid state for resolution.", args_.id);
		set_failure(ctx, err_msg);
		nlohmann::json err_json = {
			{"id", args_.id},
			{"status", "error"},
			{"message", err_msg}
		};
		return fs_utils::wrap_prompt_untrusted_data_tag("resolve_code_review_item_result", "Error: " + err_json.dump(2));
	}

	// 2. Broadcast the codereview_updated event to the global event queue.
	// NOTE: Deliberately no parent-context injection here. update/confirm/resolve are
	// bookkeeping; per-item notifications were found to be noisy and redundant (the agent
	// already gets results via toolcall returns / report_final_result).
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::codereview_updated;
		ev.payload = std::to_string(args_.id);
		ctx.queue->push(ev);
	}

	set_success(ctx);

	nlohmann::json response_json = {
		{"id", args_.id},
		{"status", "resolved"}
	};
	return fs_utils::wrap_prompt_untrusted_data_tag("resolve_code_review_item_result", response_json.dump(2));
}

} // namespace tools
