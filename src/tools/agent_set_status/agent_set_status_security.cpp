#include "agent_set_status.h"

namespace tools
{

bool agent_set_status_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

} // namespace tools
