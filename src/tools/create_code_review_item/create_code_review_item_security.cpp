#include <set>
#include "../../agentlib/tool_registry.h"
#include "create_code_review_item.h"

namespace tools
{

struct create_code_review_item_raw_args {
	std::string summary;
	std::string path;
	int line_number = 0;
	std::string line_content;
	std::string severity;
	std::string description;
	std::string proposed_fix;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(create_code_review_item_raw_args, summary, path, line_number, line_content, severity,
						description, proposed_fix);

bool create_code_review_item_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
							   std::string &out_error) const
{
	try {
		create_code_review_item_raw_args parsed = raw_json.get<create_code_review_item_raw_args>();

		if (parsed.summary.empty()) {
			out_error = "Summary cannot be empty.";
			return false;
		}
		if (parsed.path.empty()) {
			out_error = "Path cannot be empty.";
			return false;
		}
		if (parsed.severity.empty()) {
			out_error = "Severity cannot be empty.";
			return false;
		}
		if (parsed.description.empty()) {
			out_error = "Description cannot be empty.";
			return false;
		}

		static const std::set<std::string> allowed_severities = {"nit", "low", "medium", "high", "critical"};
		if (allowed_severities.find(parsed.severity) == allowed_severities.end()) {
			out_error = "Invalid severity level. Must be one of: nit, low, medium, high, critical.";
			return false;
		}

		if (parsed.line_number < 0) {
			out_error = "Line number must be >= 0.";
			return false;
		}

		// Security: Validate read access to target path
		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
			return false;
		}

		args_.summary = parsed.summary;
		args_.filename = parsed.path;
		args_.line_number = parsed.line_number;
		args_.line_content = parsed.line_content;
		args_.severity = parsed.severity;
		args_.description = parsed.description;
		args_.proposed_fix = parsed.proposed_fix;
		args_.safe_path = canonical_path;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> create_code_review_item_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<create_code_review_item_tool>(args_);
}

REGISTER_TOOL(create_code_review_item_validator)

} // namespace tools
