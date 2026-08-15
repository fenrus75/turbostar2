#include "agent_get_profile_summary.h"
#include "fs_utils.h"
#include "perf_manager.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_get_profile_summary_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string agent_get_profile_summary_tool::execute(agentlib::tool_context &ctx)
{
	auto report = turbostar::perf_manager::get_instance().get_profile_for_run(args_.run_id);
	if (report.total_samples == 0 && args_.run_id.empty()) {
		std::string perf_dir = fs_utils::get_project_perf_dir();
		report = turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir, 0, "editor", true);
	}

	if (report.total_samples == 0) {
		set_success(ctx, "No performance profile samples collected.");
		std::string res = nlohmann::json{{"run_id", args_.run_id.empty() ? "editor" : args_.run_id},
				      {"total_samples", 0},
				      {"top_functions", nlohmann::json::array()},
				      {"top_lines", nlohmann::json::array()}}
		    .dump(2);
		return fs_utils::wrap_prompt_untrusted_data_tag("agent_get_profile_summary_result", res);
	}

	nlohmann::json funcs_json = nlohmann::json::array();
	int count = 0;
	int limit = args_.limit > 0 ? args_.limit : 10;

	std::string wdir = ctx.fs_security.get_working_directory();

	for (const auto &f : report.top_functions) {
		if (count++ >= limit)
			break;
		funcs_json.push_back({
		    {"function_name", f.function_name},
		    {"file_path", fs_utils::make_relative_to_project(f.file_path, wdir)},
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
		    {"file_path", fs_utils::make_relative_to_project(l.file_path, wdir)},
		    {"line_number", l.line_number},
		    {"function_name", l.function_name},
		    {"count", l.count},
		    {"percentage", l.percentage},
		});
	}

	nlohmann::json res_json = {
	    {"run_id", args_.run_id.empty() ? "latest" : args_.run_id},
	    {"total_samples", report.total_samples},
	    {"top_functions", funcs_json},
	    {"top_lines", lines_json},
	};

	set_success(ctx, "Profile summary retrieved.");
	return fs_utils::wrap_prompt_untrusted_data_tag("agent_get_profile_summary_result", res_json.dump(2));
}

} // namespace tools
