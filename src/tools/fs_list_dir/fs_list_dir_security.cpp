#include "../../agentlib/tool_registry.h"
#include "fs_list_dir.h"

namespace tools
{

nlohmann::json fs_list_dir_validator::get_parameters_schema() const
{
#ifdef HAS_LIBMAGIC
	return {{"type", "object"},
		{"properties",
		 {{"path", {{"type", "string"}, {"description", "The path to the directory, relative to the project root."}}},
		  {"rich_metadata",
		   {{"type", "boolean"},
		    {"description", "If true, runs file header inspection to detect MIME types and format metadata (e.g. image dimensions, "
				    "ELF architectures)."}}}}},
		{"required", nlohmann::json::array({"path"})}};
#else
	return {{"type", "object"},
		{"properties",
		 {{"path", {{"type", "string"}, {"description", "The path to the directory, relative to the project root."}}}}},
		{"required", nlohmann::json::array({"path"})}};
#endif
}

bool fs_list_dir_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!args.contains("path") || !args["path"].is_string()) {
		out_error = "Missing or invalid 'path' string parameter.";
		return false;
	}
	std::string path_arg = args["path"].get<std::string>();
	if (!ctx.fs_security.validate_access(path_arg, agentlib::access_type::read, resolved_path_, out_error)) {
		return false;
	}
	rich_metadata_ = false;
#ifdef HAS_LIBMAGIC
	if (args.contains("rich_metadata") && args["rich_metadata"].is_boolean()) {
		rich_metadata_ = args["rich_metadata"].get<bool>();
	}
#endif
	return true;
}

std::unique_ptr<agentlib::llm_tool> fs_list_dir_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<fs_list_dir_tool>(resolved_path_, rich_metadata_);
}

REGISTER_TOOL(fs_list_dir_validator)

} // namespace tools
