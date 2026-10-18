#include <format>
#include "agentlib/agent_run_registry.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "perf_manager.h"
#include "tools/agent_terminate_run/agent_terminate_run.h"

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

	if (ctx.run_registry && ctx.run_registry->is_already_terminated(args_.run_id)) {
		set_success(ctx, std::format("Run ID {} was already terminated", args_.run_id));
		int linked = ctx.run_registry->get_linked_run_id(args_.run_id);
		std::string msg;
		if (linked >= 0) {
			msg = std::format("Run ID {} was already terminated when associated session (run_id: {}) was stopped.",
					  args_.run_id, linked);
		} else {
			msg = std::format("Run ID {} was already terminated in a previous call.", args_.run_id);
		}
		return msg;
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
		std::vector<int> affected_ids;
		if (ctx.run_registry) {
			affected_ids = ctx.run_registry->mark_terminated(args_.run_id);
		}
		set_success(ctx, std::format("Terminated run_id {}", args_.run_id));
		std::string msg;
		if (affected_ids.size() > 1) {
			int id1 = affected_ids[0];
			int id2 = affected_ids[1];
			auto info1 = ctx.run_registry ? ctx.run_registry->get_run_info(id1) : std::nullopt;
			int app_id = (info1 && info1->kind == agentlib::run_target_kind::app) ? id1 : id2;
			int gdb_id = (info1 && info1->kind == agentlib::run_target_kind::debugger) ? id1 : id2;
			msg = std::format("Successfully terminated application (run_id: {}) and associated debugger session (run_id: {}).",
					  app_id, gdb_id);
		} else {
			msg = std::format("Successfully terminated the application window and stopped its process (run_id: {}).",
					  args_.run_id);
		}
		if (!crash_notif.empty()) {
			msg += crash_notif;
		}
		if (!perf_notif.empty()) {
			msg += perf_notif;
		}
		return msg;
	} else {
		set_failure(ctx, std::format("Failed to terminate run_id {}", args_.run_id));
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
