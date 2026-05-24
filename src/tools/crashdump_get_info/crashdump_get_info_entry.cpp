#include "../../crashdump_manager.h"
#include "crashdump_get_info.h"

namespace tools
{

crashdump_get_info_tool::crashdump_get_info_tool(crashdump_get_info_args args) : args_(std::move(args))
{
}

bool crashdump_get_info_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string crashdump_get_info_tool::execute(agentlib::tool_context & /*ctx*/)
{
	const auto &dumps = crashdump_manager::get_instance().get_crashdumps();
	for (const auto &dump : dumps) {
		if (dump.crash_id == args_.crash_id) {
			return dump.raw_info;
		}
	}
	return "Error: No crashdump found with ID " + args_.crash_id;
}

} // namespace tools