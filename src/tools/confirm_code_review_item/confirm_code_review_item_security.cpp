#include "../../agentlib/tool_registry.h"
#include "confirm_code_review_item.h"

namespace tools {

struct confirm_code_review_item_raw_args {
	int id = 0;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	confirm_code_review_item_raw_args,
	id
);

bool confirm_code_review_item_validator::validate_args_impl(
	const nlohmann::json& raw_json,
	const agentlib::tool_context& /*ctx*/,
	std::string& out_error
) const {
	try {
		confirm_code_review_item_raw_args parsed = raw_json.get<confirm_code_review_item_raw_args>();

		if (parsed.id <= 0) {
			out_error = "ID must be a positive integer greater than 0.";
			return false;
		}

		args_.id = parsed.id;
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> confirm_code_review_item_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<confirm_code_review_item_tool>(args_);
}

REGISTER_TOOL(confirm_code_review_item_validator)

} // namespace tools
