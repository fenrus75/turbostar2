#include "agent_debug_coredump.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_debug_coredump_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_debug_coredump_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	agentlib::start_app_result res = ctx.doc_provider->start_coredump_gdb(args_.crash_id);
	if (res.gdb_run_id < 0) {
		set_failure(ctx, "Failed to debug coredump.");
		return "Error: Failed to launch coredump GDB session.";
	}

	nlohmann::json output = {
	    {"gdb_run_id", res.gdb_run_id}};

	set_success(ctx, "Started coredump debugger for crash_id " + args_.crash_id);
	return output.dump();
}

} // namespace tools
