#include "tools/agent_wait_for_app/agent_wait_for_app.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_wait_for_app_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_wait_for_app_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	agentlib::wait_for_app_result res = ctx.doc_provider->wait_for_app(args_.run_id, args_.type, args_.timeout_sec);

	nlohmann::json out = {
	    {"run_id", args_.run_id},
	    {"status", res.status},
	    {"is_alive", res.is_alive},
	    {"age_ms", res.age_ms}
	};

	if (!res.crash_notification.empty()) {
		out["crash_notification"] = res.crash_notification;
	}

	set_success(ctx, "wait_for_app completed with status: " + res.status);
	return out.dump(2);
}

} // namespace tools
