#include "agentlib/tool_registry.h"
#include <algorithm>
#include "git_log.h"

namespace tools
{

bool git_log_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
	try {
		args_.limit = 10;
		if (args.contains("limit")) {
			if (!args["limit"].is_number_integer()) {
				out_error = "Invalid 'limit' parameter: must be an integer.";
				return false;
			}
			int limit = args["limit"].get<int>();
			if (limit <= 0) {
				out_error = "'limit' parameter must be greater than 0.";
				return false;
			}
			args_.limit = std::min(limit, 1000);
		}
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> git_log_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
	return std::make_unique<git_log_tool>(args_);
}

REGISTER_TOOL(git_log_validator)

} // namespace tools
