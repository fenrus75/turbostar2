#include "list_code_review_items.h"
#include "agentlib/ai_agent.h"
#include "codereview_manager.h"
#include "fs_utils.h"
#include <format>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace tools {

list_code_review_items_tool::list_code_review_items_tool(list_code_review_items_args args)
    : agentlib::llm_tool_action("Listing code review items"), args_(std::move(args))
{
}

bool list_code_review_items_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const
{
	return true;
}

std::string list_code_review_items_tool::execute(agentlib::tool_context& ctx)
{
	bool include_resolved = args_.include_resolved;

	// Enforce visibility restriction: only verifier can see resolved items
	agentlib::agent_role role = ctx.active_agent ? ctx.active_agent->get_role() : ctx.properties.role;
	if (role != agentlib::agent_role::verifier) {
		include_resolved = false;
	}

	auto items = codereview_manager::get_instance().list_code_review_items(args_.filename, args_.severity, include_resolved);

	if (items.empty()) {
		set_success(ctx);
		return fs_utils::wrap_prompt_untrusted_data_tag("list_code_review_items_result", "No code review items found matching the filters.");
	}

	// Format as a compact Markdown table to save context tokens
	std::string table = "| ID | filename:line | summary |\n|---|---|---|\n";
	for (const auto& item : items) {
		std::string location = item.filename;
		if (item.line_number > 0) {
			location = std::format("{}:{}", item.filename, item.line_number);
		}

		// Sanitize summary to prevent broken markdown tables
		std::string sanitized_summary = item.summary;
		std::replace(sanitized_summary.begin(), sanitized_summary.end(), '|', ' ');

		table += std::format("| {} | {} | {} |\n", item.id, location, sanitized_summary);
	}

	set_success(ctx);
	return fs_utils::wrap_prompt_untrusted_data_tag("list_code_review_items_result", table);
}

} // namespace tools
