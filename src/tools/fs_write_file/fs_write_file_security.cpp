#include <nlohmann/json.hpp>
#include <optional>
#include "../../agentlib/tool_registry.h"
#include "fs_write_file.h"

namespace tools
{

// A struct to deserialize the JSON arguments into, before validation.
struct fs_write_file_raw_args {
	std::string path;
	std::string content;
	bool force_overwrite = false;
	bool append = false;
};

// Map JSON to the raw struct
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_write_file_raw_args, path, content, force_overwrite, append);

bool fs_write_file_validator::validate_args_impl(const nlohmann::json &raw_json, const agentlib::tool_context &ctx,
						 std::string &out_error) const
{
	try {
		fs_write_file_raw_args parsed = raw_json.get<fs_write_file_raw_args>();

		if (parsed.path.empty()) {
			out_error = "Path parameter cannot be empty.";
			return false;
		}

		if (parsed.force_overwrite && parsed.append) {
			out_error = "Cannot set both force_overwrite and append to true. They are mutually exclusive.";
			return false;
		}

		// CRITICAL: Perform the file security manager check (access_type::write)
		std::string canonical_path;
		if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.path = parsed.path;
		args_.content = parsed.content;
		args_.force_overwrite = parsed.force_overwrite;
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
