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
		  {"no_ask",
		   {{"type", "boolean"},
		    {"description", "Optional. If true, the tool will fail silently if the domain is not pre-approved, instead of "
				    "prompting the user for permission."}}}}},
		{"required", {"url"}}};
}

bool web_fetch_validator::validate_args_impl(const nlohmann::json &args, const agentlib::tool_context & /*ctx*/,
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
	// Check for unexpected arguments
	for (auto it = args.begin(); it != args.end(); ++it) {
		if (it.key() != "url" && it.key() != "no_ask") {
			out_error = "Unexpected parameter '" + it.key() + "' passed to tool.";
			return false;
		}
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> web_fetch_validator::create_tool_impl(const nlohmann::json &args) const
{
	std::string url = args["url"].get<std::string>();
	bool no_ask = false;
	if (args.contains("no_ask")) {
		no_ask = args["no_ask"].get<bool>();
	}
	return std::make_unique<web_fetch_tool>(url, no_ask);
}

REGISTER_TOOL(web_fetch_validator)

} // namespace tools