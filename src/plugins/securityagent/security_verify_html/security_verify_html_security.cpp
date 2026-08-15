#include <filesystem>
#include "agentlib/tool_registry.h"
#include "fs_utils.h"
#include "security_verify_html.h"

namespace tools
{

bool security_verify_html_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx,
							std::string &out_error) const
{
	resolved_paths_.clear();

	if (!args.contains("paths") || !args["paths"].is_array()) {
		out_error = "Missing or invalid 'paths' array.";
		return false;
	}

	if (args["paths"].empty()) {
		out_error = "The 'paths' array cannot be empty.";
		return false;
	}

	if (args["paths"].size() > 64) {
		out_error = "Too many paths specified (maximum 64 allowed).";
		return false;
	}

	for (const auto &path_val : args["paths"]) {
		if (!path_val.is_string()) {
			out_error = "All items in 'paths' must be strings.";
			return false;
		}

		std::string raw_path = path_val.get<std::string>();
		std::string resolved_path;

		// Require read access to verify the file.
		if (!ctx.fs_security.validate_access(raw_path, agentlib::access_type::read, resolved_path, out_error)) {
			out_error = "Access denied for path '" + raw_path + "': " + out_error;
			return false;
		}

		if (!fs_utils::is_regular_file(resolved_path)) {
			out_error = "File is not a regular file: " + raw_path;
			return false;
		}

		resolved_paths_.push_back(resolved_path);
	}

	return true;
}

std::unique_ptr<agentlib::llm_tool> security_verify_html_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<security_verify_html_tool>(resolved_paths_);
}

} // namespace tools

extern "C" {
void register_security_verify_html(void)
{
	if (std::filesystem::exists("/usr/bin/tidy")) {
		agentlib::tool_registry::get_instance().register_validator(
		    []() { return std::make_unique<tools::security_verify_html_validator>(); });
	}
}

void unregister_security_verify_html(void)
{
	agentlib::tool_registry::get_instance().unregister_validator("security_verify_html");
}
}
