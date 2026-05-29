#include "agent_write_to_run.h"

namespace tools
{

bool agent_write_to_run_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_write_to_run_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	if (ctx.doc_provider->write_to_run(args_.run_id, args_.data)) {
		set_success(ctx, "Wrote " + std::to_string(args_.data.length()) + " bytes to run_id " + std::to_string(args_.run_id));
		return "Successfully wrote input data to the PTY master.";
	} else {
		set_failure(ctx, "Failed to write data to run_id " + std::to_string(args_.run_id));
		return "Error: Run not found or process is not alive/writable.";
	}
}

} // namespace tools
