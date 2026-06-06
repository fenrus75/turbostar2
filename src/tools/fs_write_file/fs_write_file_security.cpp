#include <nlohmann/json.hpp>
#include <optional>
#include "../../agentlib/tool_registry.h"
#include "fs_write_file.h"

#include "../../agentlib/ai_agent.h"

namespace tools
{

bool fs_write_file_validator::is_allowed_in_plan_mode(const nlohmann::json& args, const agentlib::tool_context& ctx) const {
    if (!ctx.active_agent) return false;
    if (!args.contains("path") || !args["path"].is_string()) return false;
    std::string plan_file = ctx.active_agent->get_plan_file();
    return !plan_file.empty() && args["path"].get<std::string>() == plan_file;
}

// A struct to deserialize the JSON arguments into, before validation.
struct fs_write_file_raw_args {
	std::string path;
	std::string content;
	bool append = false;
};

// Map JSON to the raw struct
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_write_file_raw_args, path, content, append);

bool fs_write_file_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						 std::string &out_error) const
{
	try {
		fs_write_file_raw_args parsed = raw_json.get<fs_write_file_raw_args>();

		if (parsed.path.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}



		// CRITICAL: Perform the file security manager check (access_type::write)
		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.path = parsed.path;
		args_.content = parsed.content;
		args_.append = parsed.append;
		args_.safe_path = canonical_path;

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_write_file_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<fs_write_file_tool>(args_);
}

// Register the tool with the global registry
REGISTER_TOOL(fs_write_file_validator)

} // namespace tools
