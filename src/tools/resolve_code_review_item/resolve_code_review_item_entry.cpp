#include "resolve_code_review_item.h"
#include "agentlib/ai_agent.h"
#include "codereview_manager.h"
#include "fs_utils.h"
#include <format>
#include <nlohmann/json.hpp>

namespace tools {

resolve_code_review_item_tool::resolve_code_review_item_tool(resolve_code_review_item_args args)
    : agentlib::llm_tool_action(args.item_ids.size() == 1 ? ("Resolving code review item " + std::to_string(args.item_ids[0])) : ("Resolving " + std::to_string(args.item_ids.size()) + " code review items")), args_(std::move(args))
{
	if (interaction_ && !args_.item_ids.empty()) {
		interaction_->set_boxed(true, 5, std::to_string(args_.item_ids[0]));
	}
}

bool resolve_code_review_item_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string resolve_code_review_item_tool::execute(agentlib::tool_context& ctx)
{
	std::vector<int> resolved_ids;
	std::vector<std::string> errors;

	for (int id : args_.item_ids) {
		bool success = codereview_manager::get_instance().resolve_code_review_item(id, args_.commit_hash);
		if (!success) {
			errors.push_back(std::format("Code review item with ID {} not found or has invalid state for resolution.", id));
		} else {
			resolved_ids.push_back(id);
			if (ctx.queue) {
				editor_event ev;
				ev.type = event_type::codereview_updated;
				ev.payload = std::to_string(id);
				ctx.queue->push(ev);
			}
		}
	}

	if (resolved_ids.empty()) {
		std::string err_msg = "Error: Code review item(s) not found or have invalid state for resolution.";
		if (!errors.empty()) {
			err_msg = "Error: " + errors[0];
		}
		set_failure(ctx, err_msg);
		nlohmann::json err_json = {
			{"status", "error"},
			{"message", err_msg},
			{"errors", errors}
		};
		if (args_.item_ids.size() == 1) {
			err_json["item_id"] = args_.item_ids[0];
			err_json["id"] = args_.item_ids[0];
		}
		return fs_utils::wrap_prompt_untrusted_data_tag("resolve_code_review_item_result", "Error: " + err_json.dump(2));
	}

	set_success(ctx);

	nlohmann::json response_json = {
		{"status", "resolved"}
	};
	if (args_.item_ids.size() == 1) {
		response_json["item_id"] = resolved_ids[0];
		response_json["id"] = resolved_ids[0];
	} else {
		response_json["item_ids"] = resolved_ids;
		response_json["resolved_items"] = resolved_ids;
		response_json["item_id"] = resolved_ids[0];
		response_json["id"] = resolved_ids[0];
	}
	if (!errors.empty()) {
		response_json["warnings"] = errors;
	}
	return fs_utils::wrap_prompt_untrusted_data_tag("resolve_code_review_item_result", response_json.dump(2));
}

} // namespace tools
