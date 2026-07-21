#include "agent_start_app.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_start_app_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_start_app_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	agentlib::start_app_result res = ctx.doc_provider->start_app(args_.args, args_.debugger, false);
	if (res.app_run_id < 0) {
		set_failure(ctx, "Failed to start application.");
		return "Error: Failed to start application process.";
	}

	nlohmann::json output = {
	    {"app_run_id", res.app_run_id},
	    {"gdb_run_id", res.gdb_run_id >= 0 ? nlohmann::json(res.gdb_run_id) : nlohmann::json(nullptr)}};

	if (args_.wait_for_time > 0) {
		agentlib::wait_for_app_result wait_res = ctx.doc_provider->wait_for_app(res.app_run_id, "ended", args_.wait_for_time);
		output["status"] = wait_res.status;
		output["is_alive"] = wait_res.is_alive;
		output["age_ms"] = wait_res.age_ms;
		if (!wait_res.crash_notification.empty()) {
			output["crash_notification"] = wait_res.crash_notification;
		}
		set_success(ctx, "Started run_id " + std::to_string(res.app_run_id) + " (status: " + wait_res.status + ")");
	} else {
		set_success(ctx, "Started run_id " + std::to_string(res.app_run_id));
	}

	return output.dump(2);
}

} // namespace tools
