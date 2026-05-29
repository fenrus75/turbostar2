#include "agent_terminate_run.h"

namespace tools
{

bool agent_terminate_run_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_terminate_run_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	if (ctx.doc_provider->terminate_run(args_.run_id)) {
		set_success(ctx, "Terminated run_id " + std::to_string(args_.run_id));
		return "Successfully terminated the application window and stopped its process.";
	} else {
		set_failure(ctx, "Failed to terminate run_id " + std::to_string(args_.run_id));
		return "Error: Run ID not found or already stopped.";
	}
}

} // namespace tools
