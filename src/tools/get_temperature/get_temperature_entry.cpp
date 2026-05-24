#include "get_temperature.h"

namespace tools
{

get_temperature_tool::get_temperature_tool(std::string location) : location_(std::move(location))
{
}

bool get_temperature_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	// Nothing context-specific to restrict here for a simple weather tool.
	return true;
}

std::string get_temperature_tool::execute(agentlib::tool_context & /*ctx*/)
{
	// In a real implementation, we might call an external API here.
	return "It is currently 42F in " + location_ + ".";
}

} // namespace tools
