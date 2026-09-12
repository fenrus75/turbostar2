#include "agentlib/tool_registry.h"
#include "git_unstage.h"

namespace tools
{

bool git_unstage_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	resolved_paths_.clear();

	if (!args.contains("paths")) {
		out_error = "Missing required argument 'paths'.";
		return false;
	}

	std::vector<std::string> untrusted_raw_paths;
	if (args["paths"].is_array()) {
		if (args["paths"].empty()) {
			out_error = "The 'paths' array cannot be empty.";
			return false;
		}
		for (const auto &untrusted_val : args["paths"]) {
			if (!untrusted_val.is_string()) {
				out_error = "All items in 'paths' must be strings.";
				return false;
			}
			untrusted_raw_paths.push_back(untrusted_val.get<std::string>());
		}
	} else if (args["paths"].is_string()) {
		std::string s = args["paths"].get<std::string>();
		if (s.empty()) {
			out_error = "The 'paths' parameter cannot be empty.";
			return false;
		}
		untrusted_raw_paths.push_back(std::move(s));
	} else {
		out_error = "Argument 'paths' must be an array of strings or a single string.";
		return false;
	}

	for (const auto &untrusted_path : untrusted_raw_paths) {
		if (untrusted_path.find("://") != std::string::npos) {
			out_error = "Validation Error: git_unstage cannot operate on virtual VFS paths ('" + untrusted_path + "').";
			return false;
		}
		if (untrusted_path.find(".git") != std::string::npos) {
			out_error = "Security Violation: Cannot unstage paths inside .git directory.";
			return false;
		}

		std::string resolved_path;

		// Stage 1 Security: Validate against the file_security_manager.
		if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, resolved_path, out_error)) {
			out_error = "Access denied for path '" + untrusted_path + "': " + out_error;
			return false;
		}

		resolved_paths_.push_back(resolved_path);
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> git_unstage_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<git_unstage_tool>(resolved_paths_);
}

REGISTER_TOOL(git_unstage_validator)

} // namespace tools
