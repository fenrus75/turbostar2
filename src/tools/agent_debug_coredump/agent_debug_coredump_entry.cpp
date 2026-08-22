#include "agent_debug_coredump.h"
#include "crashdump_manager.h"
#include <nlohmann/json.hpp>
#include <format>

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

	std::string instructions = std::format(
	    "Coredump GDB session started (gdb_run_id: {}).\n\n"
	    "Next Steps:\n"
	    "1. Run backtrace to inspect call stack and locate the crash frame number N:\n"
	    "   agent_write_to_run(run_id={}, data=\"bt\\n\", output=true)\n"
	    "2. Select the crash frame N and inspect local variables / parameters:\n"
	    "   agent_write_to_run(run_id={}, data=\"frame <N>\\ninfo locals\\n\", output=true)\n"
	    "3. Clean up when finished:\n"
	    "   agent_terminate_run(run_id={})\n\n"
	    "Full workflow reference: system://tools_detailed.md?search=agent_debug_coredump",
	    res.gdb_run_id, res.gdb_run_id, res.gdb_run_id, res.gdb_run_id);

	nlohmann::json output = {
	    {"gdb_run_id", res.gdb_run_id},
	    {"instructions", instructions}};

	set_success(ctx, std::format("Started coredump debugger for crash_id {}", args_.crash_id));
	return output.dump();
}



} // namespace tools
