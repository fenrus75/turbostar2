#include "../../agentlib/tool_registry.h"
#include "git_add.h"

namespace tools
{

bool git_add_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	resolved_paths_.clear();

	if (!args.contains("paths")) {
		out_error = "Missing required argument 'paths' (or 'files', 'path', 'file').";
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
		std::string resolved_path;

		// Stage 1 Security: Validate against the file_security_manager.
		// Even though git add doesn't strictly 'read' the file into our process,
		// it exposes file metadata to the git index, so we require read permission.
		if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, resolved_path, out_error)) {
			out_error = "Access denied for path '" + untrusted_path + "': " + out_error;
			return false;
		}

		resolved_paths_.push_back(resolved_path);
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> git_add_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<git_add_tool>(resolved_paths_);
}

REGISTER_TOOL(git_add_validator)

} // namespace tools
