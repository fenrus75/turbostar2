#include "agent_get_profile_summary.h"
#include "../../fs_utils.h"
#include "../../perf_manager.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_get_profile_summary_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string agent_get_profile_summary_tool::execute(agentlib::tool_context &ctx)
{
	auto report = turbostar::perf_manager::get_instance().get_active_profile();
	if (report.total_samples == 0) {
		std::string perf_dir = fs_utils::get_project_perf_dir();
		report = turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir, 0, true);
	}

	if (report.total_samples == 0) {
		set_success(ctx, "No performance profile samples collected.");
		return nlohmann::json{{"total_samples", 0}, {"top_functions", nlohmann::json::array()}, {"top_lines", nlohmann::json::array()}}.dump(2);
	}

	nlohmann::json funcs_json = nlohmann::json::array();
	int count = 0;
	int limit = args_.limit > 0 ? args_.limit : 10;

	for (const auto &f : report.top_functions) {
		if (count++ >= limit)
			break;
		funcs_json.push_back({
		    {"function_name", f.function_name},
		    {"file_path", f.file_path},
		    {"line_number", f.line_number},
		    {"count", f.count},
		    {"percentage", f.percentage},
		});
	}

	nlohmann::json lines_json = nlohmann::json::array();
	count = 0;
	for (const auto &l : report.top_lines) {
		if (count++ >= limit)
			break;
		lines_json.push_back({
		    {"file_path", l.file_path},
		    {"line_number", l.line_number},
		    {"count", l.count},
		    {"percentage", l.percentage},
		});
	}

	nlohmann::json output = {
	    {"total_samples", report.total_samples},
	    {"top_functions", funcs_json},
	    {"top_lines", lines_json},
	};

	set_success(ctx, "Retrieved profile summary (" + std::to_string(report.total_samples) + " samples)");
	return output.dump(2);
}

} // namespace tools
