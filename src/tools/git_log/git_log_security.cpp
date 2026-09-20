#include "agentlib/tool_registry.h"
#include <algorithm>
#include <format>
#include "git_log.h"

namespace tools
{

bool git_log_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const {
	try {
		args_.limit = 10;
		args_.safe_path.clear();
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

		if (args.contains("path")) {
			if (!args["path"].is_string()) {
				out_error = "Invalid 'path' parameter: must be a string.";
				return false;
			}
			std::string untrusted_path = args["path"].get<std::string>();
			if (!untrusted_path.empty()) {
				std::string safe_path;
				if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, out_error)) {
					return false;
				}
				if (untrusted_path != ".") {
					args_.safe_path = safe_path;
				}
			}
		}

		return true;
	} catch (const std::exception& e) {
		out_error = std::format("Invalid arguments: {}", e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> git_log_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
	return std::make_unique<git_log_tool>(args_);
}

REGISTER_TOOL(git_log_validator)

} // namespace tools
