#include "../../crashdump_manager.h"
#include "crashdump_list.h"

namespace tools
{

bool crashdump_list_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string crashdump_list_tool::execute(agentlib::tool_context & /*ctx*/)
{
	return crashdump_manager::get_instance().get_markdown_table();
}

} // namespace tools