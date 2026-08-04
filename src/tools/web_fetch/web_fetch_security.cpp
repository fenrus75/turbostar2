#include <regex>
#include "../../agentlib/tool_registry.h"
#include "web_fetch.h"

namespace tools
{

nlohmann::json web_fetch_validator::get_parameters_schema() const
{
	return {{"type", "object"},
		{"properties",
		 {{"url", {{"type", "string"}, {"description", "The full URL to fetch (must start with http:// or https://)."}}},
		  {"output_path",
		   {{"type", "string"},
		    {"description", "Optional. The relative file path under the project workspace or VFS URI (e.g., 'tmp://file.txt') to save the fetched content."}}},
		  {"filter",
		   {{"type", "string"},
		    {"description", "Optional. A named content processing filter to apply (e.g. 'html_to_markdown' to reduce size/context)."}}},
		  {"no_ask",
		   {{"type", "boolean"},
		    {"description", "Optional. If true, the tool will fail silently if the domain is not pre-approved, instead of "
				    "prompting the user for permission."}}}}},
		{"required", {"url"}}};
}

bool web_fetch_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context &ctx,
					     std::string &out_error) const
{
	if (!args.is_object()) {
		out_error = "Arguments must be a JSON object.";
		return false;
	}
	if (!args.contains("url") || !args["url"].is_string()) {
		out_error = "Missing or invalid 'url' argument.";
		return false;
	}
	std::string url = args["url"].get<std::string>();
	if (!url.starts_with("http://") && !url.starts_with("https://")) {
		out_error = "URL must start with http:// or https://";
		return false;
	}
	if (args.contains("no_ask") && !args["no_ask"].is_boolean()) {
		out_error = "Invalid 'no_ask' argument (must be boolean).";
		return false;
	}

	std::string output_path;
	std::string safe_output_path;
	if (args.contains("output_path")) {
		if (!args["output_path"].is_string()) {
			out_error = "Invalid 'output_path' argument (must be string).";
			return false;
		}
		output_path = args["output_path"].get<std::string>();
		if (output_path.empty()) {
			out_error = "output_path cannot be empty.";
			return false;
		}
		if (!ctx.fs_security.validate_access(output_path, agentlib::access_type::write, safe_output_path, out_error)) {
			return false;
		}
	}

	std::string filter;
	if (args.contains("filter")) {
		if (!args["filter"].is_string()) {
			out_error = "Invalid 'filter' argument (must be string).";
			return false;
		}
		filter = args["filter"].get<std::string>();
	}

	// Check for unexpected arguments
	for (auto it = args.begin(); it != args.end(); ++it) {
		if (it.key() != "url" && it.key() != "no_ask" && it.key() != "output_path" && it.key() != "filter") {
			out_error = "Unexpected parameter '" + it.key() + "' passed to tool.";
			return false;
		}
	}

	parsed_args_.url = url;
	parsed_args_.output_path = output_path;
	parsed_args_.safe_output_path = safe_output_path;
	parsed_args_.filter = filter;
	parsed_args_.no_ask = args.contains("no_ask") ? args["no_ask"].get<bool>() : false;

	return true;
}

std::unique_ptr<agentlib::llm_tool> web_fetch_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	return std::make_unique<web_fetch_tool>(parsed_args_);
}

REGISTER_TOOL(web_fetch_validator)

} // namespace tools