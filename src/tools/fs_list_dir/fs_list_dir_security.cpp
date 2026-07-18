#include "../../agentlib/tool_registry.h"
#include "fs_list_dir.h"

namespace tools
{

nlohmann::json fs_list_dir_validator::get_parameters_schema() const
{
	return {{"type", "object"},
		{"properties",
		 {{"path", {{"type", "string"}, {"description", "The path to the directory, relative to the project root."}}},
		  {"rich_metadata",
		   {{"type", "boolean"},
		    {"description", "If true, runs file header inspection to detect MIME types and format metadata (e.g. image dimensions, "
				    "ELF architectures)."}}},
		  {"limit",
		   {{"type", "integer"},
		    {"description", "Optional. Maximum number of files to return in the list. Defaults to 100."},
		    {"default", 100}}},
		  {"offset",
		   {{"type", "integer"},
		    {"description", "Optional. Starting offset for pagination. Defaults to 0."},
		    {"default", 0}}}}},
		{"required", nlohmann::json::array({"path"})}};
}

bool fs_list_dir_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!args.contains("path") || !args["path"].is_string()) {
		out_error = "Missing or invalid 'path' string parameter.";
		return false;
	}
	std::string path_arg = args["path"].get<std::string>();
	if (!ctx.fs_security.validate_access(path_arg, agentlib::access_type::read, args_.path, out_error)) {
		return false;
	}
	args_.rich_metadata = false;
	if (args.contains("rich_metadata") && args["rich_metadata"].is_boolean()) {
		args_.rich_metadata = args["rich_metadata"].get<bool>();
	}
	args_.limit = 100;
	if (args.contains("limit")) {
		if (!args["limit"].is_number_integer()) {
			out_error = "Invalid 'limit' parameter: must be an integer.";
			return false;
		}
		args_.limit = args["limit"].get<int>();
		if (args_.limit <= 0) {
			out_error = "'limit' parameter must be greater than 0.";
			return false;
		}
	}
	args_.offset = 0;
	if (args.contains("offset")) {
		if (!args["offset"].is_number_integer()) {
			out_error = "Invalid 'offset' parameter: must be an integer.";
			return false;
		}
		args_.offset = args["offset"].get<int>();
		if (args_.offset < 0) {
			out_error = "'offset' parameter must be greater than or equal to 0.";
			return false;
		}
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> fs_list_dir_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<fs_list_dir_tool>(args_);
}

REGISTER_TOOL(fs_list_dir_validator)

} // namespace tools
