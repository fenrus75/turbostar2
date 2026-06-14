#include "../../agentlib/tool_registry.h"
#include "resolve_code_review_item.h"

namespace tools {

struct resolve_code_review_item_raw_args {
	int id = 0;
	std::string commit_hash;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	resolve_code_review_item_raw_args,
	id,
	commit_hash
);

bool resolve_code_review_item_validator::validate_args_impl(
	const nlohmann::json& raw_json,
	const agentlib::tool_context& /*ctx*/,
	std::string& out_error
) const {
	try {
		resolve_code_review_item_raw_args parsed = raw_json.get<resolve_code_review_item_raw_args>();

		if (parsed.id <= 0) {
			out_error = "ID must be a positive integer greater than 0.";
			return false;
		}

		if (parsed.commit_hash.empty()) {
			out_error = "Commit hash must not be empty.";
			return false;
		}

		args_.id = parsed.id;
		args_.commit_hash = parsed.commit_hash;
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> resolve_code_review_item_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<resolve_code_review_item_tool>(args_);
}

REGISTER_TOOL(resolve_code_review_item_validator)

} // namespace tools
