#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "get_code_review_item.h"

namespace tools {

bool get_code_review_item_validator::validate_args_impl(
	const nlohmann::json& raw_json,
	const agentlib::tool_context& /*ctx*/,
	std::string& out_error
) const {
	try {
		int id = 0;
		if (raw_json.contains("item_id") && raw_json["item_id"].is_number_integer()) {
			id = raw_json["item_id"].get<int>();
		} else if (raw_json.contains("id") && raw_json["id"].is_number_integer()) {
			id = raw_json["id"].get<int>();
		} else {
			out_error = "Missing required argument: 'item_id'.";
			return false;
		}

		if (id <= 0) {
			out_error = "item_id must be a positive integer greater than 0.";
			return false;
		}

		args_.id = id;
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> get_code_review_item_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<get_code_review_item_tool>(args_);
}

REGISTER_TOOL(get_code_review_item_validator)

} // namespace tools
