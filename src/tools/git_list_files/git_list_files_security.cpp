#include "../../agentlib/tool_registry.h"
#include "git_list_files.h"

namespace tools {

bool git_list_files_validator::validate_args_impl(const nlohmann::json &untrusted_args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	args_ = git_list_files_args();

	if (untrusted_args.contains("path") && untrusted_args["path"].is_string()) {
		std::string untrusted_path = untrusted_args["path"].get<std::string>();
		if (!untrusted_path.empty() && untrusted_path != ".") {
			std::string safe_path;
			if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, out_error)) {
				return false;
			}
			args_.safe_path = safe_path;
		} else {
			args_.safe_path = ".";
		}
	} else {
		args_.safe_path = ".";
	}

	if (untrusted_args.contains("pattern") && untrusted_args["pattern"].is_string()) {
		args_.pattern = untrusted_args["pattern"].get<std::string>();
	}

	if (untrusted_args.contains("limit") && untrusted_args["limit"].is_number_integer()) {
		int lim = untrusted_args["limit"].get<int>();
		if (lim > 0) {
			args_.limit = std::min(lim, 5000);
		}
	}

	if (untrusted_args.contains("untracked") && untrusted_args["untracked"].is_boolean()) {
		args_.untracked = untrusted_args["untracked"].get<bool>();
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> git_list_files_validator::create_tool_impl(const nlohmann::json & /*untrusted_args*/) const
{
	return std::make_unique<git_list_files_tool>(args_);
}

REGISTER_TOOL(git_list_files_validator)

} // namespace tools
