#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/interactions/action.h"
#include "../../codereview_manager.h"
#include "create_code_review_item.h"

namespace tools
{

create_code_review_item_tool::create_code_review_item_tool(create_code_review_item_args args)
    : llm_tool_action("Creating code review item for " + args.filename), args_(std::move(args))
{
	if (interaction_) {
		interaction_->set_boxed(true, 5, args_.filename);
	}
}

bool create_code_review_item_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string create_code_review_item_tool::execute(agentlib::tool_context &ctx)
{
	// 1. Resolve line content from file if line number was specified but no content was supplied
	if (args_.line_number > 0 && args_.line_content.empty() && !args_.safe_path.empty()) {
		std::ifstream f(args_.safe_path);
		if (f.is_open()) {
			std::string line;
			int current = 1;
			while (std::getline(f, line)) {
				if (current == args_.line_number) {
					args_.line_content = line;
					break;
				}
				current++;
			}
			f.close();
		}
	}

	// 2. Create the code review item in the manager
	int item_id = codereview_manager::get_instance().create_code_review_item(
	    args_.summary, args_.filename, args_.line_number, args_.line_content, args_.severity, args_.description, args_.proposed_fix);

	// 3. Optional parent notification, kept deliberately SHORT (single line) so it serves as a
	// lightweight "an item was filed" signal rather than duplicating the full finding (which is
	// persisted in the DB and surfaced via list_code_review_items / toolcall returns).
	// Suppressed entirely when the calling agent opts out via set_suppress_parent_injection()
	// (e.g. synchronous perform_code_review, where results come back through the toolcall).
	if (ctx.active_agent && !ctx.active_agent->is_suppress_parent_injection()) {
		auto parent = ctx.active_agent->get_parent();
		if (parent) {
			std::string parent_msg = std::format("Subagent created code review item #{} ({}): {}:{} - {}", item_id,
							     args_.severity, args_.filename, args_.line_number, args_.summary);
			parent->inject_context("user", parent_msg, false);
		}
	}

	// 4. Broadcast the codereview_updated event to the global event queue
	if (ctx.queue) {
		editor_event ev;
		ev.type = event_type::codereview_updated;
		ev.key_code = item_id;
		ctx.queue->push(ev);
	}

	set_success(ctx);

	nlohmann::json response_json = {{"id", item_id},
					{"status", "created"},
					{"filename", args_.filename},
					{"line_number", args_.line_number},
					{"line_content", args_.line_content}};
	return response_json.dump(2);
}

} // namespace tools
