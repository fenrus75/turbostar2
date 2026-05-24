#include "crashdump_clear.h"
#include "../../crashdump_manager.h"

namespace tools
{

bool crashdump_clear_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string crashdump_clear_tool::execute(agentlib::tool_context &ctx)
{
	crashdump_manager::get_instance().clear_all();
	set_success(ctx, "all crash dumps cleared");
	return "Successfully cleared all crash dumps.";
}

} // namespace tools
