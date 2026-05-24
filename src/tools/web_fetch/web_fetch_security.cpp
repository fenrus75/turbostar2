#include <regex>
#include "../../agentlib/tool_registry.h"
#include "web_fetch.h"

namespace tools
{

bool web_fetch_validator::validate_string_arg(const std::string &arg, const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (!arg.starts_with("http://") && !arg.starts_with("https://")) {
		out_error = "URL must start with http:// or https://";
		return false;
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> web_fetch_validator::create_tool_from_string(const std::string &arg) const
{
	return std::make_unique<web_fetch_tool>(arg);
}

REGISTER_TOOL(web_fetch_validator)

} // namespace tools