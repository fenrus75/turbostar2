#include "../../agentlib/tool_registry.h"
#include "security_review_with_agent.h"

namespace tools
{

struct security_review_with_agent_raw_args {
	std::vector<std::string> files;
	std::string result_file;
	std::string instructions;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(security_review_with_agent_raw_args, files, result_file, instructions);

bool security_review_with_agent_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
							       std::string &out_error) const
{
	try {
		security_review_with_agent_raw_args parsed = raw_json.get<security_review_with_agent_raw_args>();

		if (parsed.files.empty()) {
			out_error = "Files array must not be empty.";
			return false;
		}

		// Security path validation: check each file under read access
		for (const auto &file_path : parsed.files) {
			std::string resolved_path;
			if (!ctx.fs_security.validate_access(file_path, agentlib::access_type::read, resolved_path, out_error)) {
				return false;
			}
		}

		// Security path validation: check result_file under write access if configured
		if (!parsed.result_file.empty()) {
			std::string resolved_path;
			if (!ctx.fs_security.validate_access(parsed.result_file, agentlib::access_type::write, resolved_path, out_error)) {
				return false;
			}
		}

		args_.files = parsed.files;
		args_.result_file = parsed.result_file;
		args_.instructions = parsed.instructions;
		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> security_review_with_agent_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<security_review_with_agent_tool>(args_);
}

REGISTER_TOOL(security_review_with_agent_validator)

} // namespace tools
