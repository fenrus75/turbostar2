#include <algorithm>
#include <format>
#include "agentlib/tool_registry.h"
#include "git_log.h"

namespace tools
{

bool git_log_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	try {
		args_.limit = 10;
		args_.safe_path.clear();
		args_.commit_id.clear();
		args_.show_patch = false;
		args_.stat = false;

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

		if (args.contains("commit_id")) {
			if (!args["commit_id"].is_string()) {
				out_error = "Invalid 'commit_id' parameter: must be a string.";
				return false;
			}
			std::string untrusted_commit = args["commit_id"].get<std::string>();
			if (!untrusted_commit.empty()) {
				for (char c : untrusted_commit) {
					if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-' && c != '^' &&
					    c != '~' && c != '@' && c != '/') {
						out_error = "Invalid characters in 'commit_id'. Must be a valid git commit or ref.";
						return false;
					}
				}
				args_.commit_id = untrusted_commit;
			}
		}

		if (args.contains("show_patch")) {
			if (!args["show_patch"].is_boolean()) {
				out_error = "Invalid 'show_patch' parameter: must be a boolean.";
				return false;
			}
			args_.show_patch = args["show_patch"].get<bool>();
		}

		if (args.contains("stat")) {
			if (!args["stat"].is_boolean()) {
				out_error = "Invalid 'stat' parameter: must be a boolean.";
				return false;
			}
			args_.stat = args["stat"].get<bool>();
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
	} catch (const std::exception &e) {
		out_error = std::format("Invalid arguments: {}", e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> git_log_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<git_log_tool>(args_);
}

REGISTER_TOOL(git_log_validator)

} // namespace tools
