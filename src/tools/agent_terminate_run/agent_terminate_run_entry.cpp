#include "tools/agent_terminate_run/agent_terminate_run.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "perf_manager.h"

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

	std::string run_id_str = "run_" + std::to_string(args_.run_id);
	auto report = turbostar::perf_manager::get_instance().get_profile_for_run(run_id_str);
	if (report.total_samples == 0) {
		std::string perf_dir = fs_utils::get_project_perf_dir();
		report = turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir, 0, run_id_str, true);
	}
	std::string perf_notif;
	if (report.total_samples > 0) {
		perf_notif = "\n\nPerformance profile data is available, use agent_get_profile_summary to retrieve this data.";
	}

	if (ctx.doc_provider->terminate_run(args_.run_id)) {
		set_success(ctx, "Terminated run_id " + std::to_string(args_.run_id));
		std::string msg = "Successfully terminated the application window and stopped its process.";
		if (!crash_notif.empty()) {
			msg += crash_notif;
		}
		if (!perf_notif.empty()) {
			msg += perf_notif;
		}
		return msg;
	} else {
		set_failure(ctx, "Failed to terminate run_id " + std::to_string(args_.run_id));
		std::string msg = "Error: Run ID not found or already stopped.";
		if (!crash_notif.empty()) {
			msg += crash_notif;
		}
		if (!perf_notif.empty()) {
			msg += perf_notif;
		}
		return msg;
	}
}

} // namespace tools
