#include <filesystem>
#include "agentlib/tool_registry.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include "flag_as_error.h"

namespace tools
{

REGISTER_TOOL(flag_as_error_security)

nlohmann::json flag_as_error_security::get_parameters_schema() const
{
	return {{"type", "object"},
		{"properties",
		 {{"path", {{"type", "string"}, {"description", "Relative path under the project workspace or VFS URI (e.g., 'tmp://file.txt'). The relative path to the file."}}},
		  {"line", {{"type", "integer"}, {"description", "The 1-based line number of the error."}}},
		  {"column", {{"type", "integer"}, {"description", "The 1-based start column number of the error. Use 1 if unknown."}}},
		  {"length",
		   {{"type", "integer"},
		    {"description", "The length of the error highlight in characters. Use 0 to highlight the whole line."}}},
		  {"error_string", {{"type", "string"}, {"description", "The description of the error."}}},
		  {"is_warning", {{"type", "boolean"}, {"description", "True if this is a warning, false if it is a hard error."}}}}},
		{"required", {"path", "line", "column", "length", "error_string", "is_warning"}}};
}

bool flag_as_error_security::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!args.contains("path") || !args["path"].is_string() || !args.contains("line") || !args["line"].is_number() ||
	    !args.contains("column") || !args["column"].is_number() || !args.contains("length") || !args["length"].is_number() ||
	    !args.contains("error_string") || !args["error_string"].is_string() || !args.contains("is_warning") ||
	    !args["is_warning"].is_boolean()) {
		out_error = "Invalid arguments: Requires path (string), line (int), column (int), length (int), error_string (string), "
			    "is_warning (bool).";
		return false;
	}

	if (args["line"].get<int>() < 1) {
		out_error = "Invalid argument: 'line' must be >= 1.";
		return false;
	}

	if (args["column"].get<int>() < 1) {
		out_error = "Invalid argument: 'column' must be >= 1.";
		return false;
	}

	if (args["length"].get<int>() < 0) {
		out_error = "Invalid argument: 'length' must be >= 0.";
		return false;
	}

	std::string error_str = args["error_string"].get<std::string>();
	if (error_str.length() > 2000) {
		out_error = "Validation Error: 'error_string' parameter exceeds maximum length of 2000 characters.";
		return false;
	}
	if (!fs_utils::is_safe_for_ui(error_str)) {
		out_error = "Security Violation: 'error_string' contains unsafe control characters or escape sequences.";
		return false;
	}

	std::string path = args["path"].get<std::string>();
	std::string safe_path;
	if (!ctx.fs_security.validate_access(path, agentlib::access_type::read, safe_path, out_error)) {
		return false;
	}

	parsed_safe_path_ = safe_path;
	parsed_error_string_ = error_str;
	return true;
}

std::unique_ptr<agentlib::llm_tool> flag_as_error_security::create_tool_impl(const nlohmann::json &args) const
{
	flag_as_error_args t_args;
	t_args.safe_path = parsed_safe_path_;
	t_args.line = args["line"].get<int>();
	t_args.column = args["column"].get<int>();
	t_args.length = args["length"].get<int>();
	t_args.error_string = parsed_error_string_;
	t_args.is_warning = args["is_warning"].get<bool>();

	return std::make_unique<flag_as_error_tool>(t_args);
}

} // namespace tools
