#include "tools/agent_terminate_run/agent_terminate_run.h"
#include "crashdump_manager.h"

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

	crashdump_manager::get_instance().refresh("");
	auto dumps = crashdump_manager::get_instance().get_crashdumps_for_run(args_.run_id);
	std::string crash_notif = crashdump_manager::format_crash_notification(dumps);

	if (ctx.doc_provider->terminate_run(args_.run_id)) {
		set_success(ctx, "Terminated run_id " + std::to_string(args_.run_id));
		std::string msg = "Successfully terminated the application window and stopped its process.";
		if (!crash_notif.empty()) {
			msg += crash_notif;
		}
		return msg;
	} else {
		set_failure(ctx, "Failed to terminate run_id " + std::to_string(args_.run_id));
		std::string msg = "Error: Run ID not found or already stopped.";
		if (!crash_notif.empty()) {
			msg += crash_notif;
		}
		return msg;
	}
}

} // namespace tools
