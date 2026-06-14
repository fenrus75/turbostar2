#include "../../agentlib/tool_registry.h"
#include "list_code_review_items.h"

namespace tools {

struct list_code_review_items_raw_args {
	std::string filename;
	std::string severity;
	bool include_resolved = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	list_code_review_items_raw_args,
	filename,
	severity,
	include_resolved
);

bool list_code_review_items_validator::validate_args_impl(
	const nlohmann::json& raw_json,
	const agentlib::tool_context& /*ctx*/,
	std::string& out_error
) const {
	try {
		list_code_review_items_raw_args parsed = raw_json.get<list_code_review_items_raw_args>();

		args_.filename = parsed.filename;
		args_.severity = parsed.severity;
		args_.include_resolved = parsed.include_resolved;
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> list_code_review_items_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<list_code_review_items_tool>(args_);
}

REGISTER_TOOL(list_code_review_items_validator)

} // namespace tools
