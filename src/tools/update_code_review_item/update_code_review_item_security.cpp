#include "agentlib/tool_registry.h"
#include "update_code_review_item.h"
#include "fs_utils.h"
#include <set>

namespace tools {

struct update_code_review_item_raw_args {
	int id = 0;
	std::optional<std::string> state;
	std::optional<std::string> severity;
	std::optional<std::string> description;
	std::optional<std::string> proposed_fix;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	update_code_review_item_raw_args,
	id, state, severity, description, proposed_fix
);

bool update_code_review_item_validator::validate_args_impl(
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

		update_code_review_item_raw_args parsed = raw_json.get<update_code_review_item_raw_args>();
		parsed.id = id;

		if (parsed.state) {
			static const std::set<std::string> allowed_states = {
				"invalid", "new", "confirmed", "disputed", "stale", "resolved", "verified-fixed"
			};
			if (allowed_states.find(*parsed.state) == allowed_states.end()) {
				out_error = "Invalid state. Must be one of: invalid, new, confirmed, disputed, stale, resolved, verified-fixed.";
				return false;
			}
		}

		if (parsed.severity) {
			static const std::set<std::string> allowed_severities = {
				"nit", "low", "medium", "high", "critical"
			};
			if (allowed_severities.find(*parsed.severity) == allowed_severities.end()) {
				out_error = "Invalid severity level. Must be one of: nit, low, medium, high, critical.";
				return false;
			}
		}

		if (parsed.description) {
			if (parsed.description->length() > 16384) {
				out_error = "Validation Error: description exceeds maximum length of 16384 characters.";
				return false;
			}
			if (!fs_utils::is_safe_for_ui(*parsed.description)) {
				out_error = "Security Violation: description contains unsafe control characters.";
				return false;
			}
		}

		if (parsed.proposed_fix) {
			if (parsed.proposed_fix->length() > 16384) {
				out_error = "Validation Error: proposed_fix exceeds maximum length of 16384 characters.";
				return false;
			}
			if (!fs_utils::is_safe_for_ui(*parsed.proposed_fix)) {
				out_error = "Security Violation: proposed_fix contains unsafe control characters.";
				return false;
			}
		}

		args_.id = parsed.id;
		args_.state = parsed.state;
		args_.severity = parsed.severity;
		args_.description = parsed.description;
		args_.proposed_fix = parsed.proposed_fix;

		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> update_code_review_item_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<update_code_review_item_tool>(args_);
}

REGISTER_TOOL(update_code_review_item_validator)

} // namespace tools
