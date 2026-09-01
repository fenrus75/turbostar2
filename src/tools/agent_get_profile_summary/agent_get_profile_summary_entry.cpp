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

	std::string run_id_str = args_.run_id.empty() ? "latest" : args_.run_id;

	if (args_.format == "json") {
		if (report.total_samples == 0) {
			set_success(ctx, "No performance profile samples collected.");
			std::string res = nlohmann::json{{"run_id", run_id_str},
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
		    {"run_id", run_id_str},
		    {"total_samples", report.total_samples},
		    {"top_functions", funcs_json},
		    {"top_lines", lines_json},
		};

		set_success(ctx, "Profile summary retrieved.");
		return fs_utils::wrap_prompt_untrusted_data_tag("agent_get_profile_summary_result", res_json.dump(2));
	}

	// Default: Markdown table format
	if (report.total_samples == 0) {
		set_success(ctx, "No performance profile samples collected.");
		return fs_utils::wrap_prompt_untrusted_data_tag("agent_get_profile_summary_result",
			std::format("### CPU Performance Profile Summary (Run ID: `{}`)\n*No CPU cycle samples collected for this run.*", run_id_str));
	}

	std::string wdir = ctx.fs_security.get_working_directory();
	int limit = args_.limit > 0 ? args_.limit : 10;

	std::ostringstream ss;
	ss << std::format("### CPU Performance Profile Summary (Run ID: `{}`)\n", run_id_str);
	ss << std::format("**Total Samples Collected:** {} Hardware CPU Cycles\n\n", report.total_samples);

	if (!report.top_functions.empty()) {
		ss << "#### Top Functions (Ranked by CPU Cycles)\n\n";
		ss << "| Rank | Function | Source File | Line | Samples | % CPU Cycles |\n";
		ss << "| :---: | :--- | :--- | :---: | :---: | :---: |\n";
		int rank = 1;
		for (const auto &f : report.top_functions) {
			if (rank > limit) break;
			std::string rel_file = fs_utils::make_relative_to_project(f.file_path, wdir);
			std::string func_name = f.function_name.empty() ? "`<unknown>`" : std::format("`{}`", f.function_name);
			std::string file_str = rel_file.empty() ? "`<unknown>`" : std::format("`{}`", rel_file);
			ss << std::format("| {} | {} | {} | {} | {} | **{:.2f}%** |\n",
				rank++, func_name, file_str, f.line_number, f.count, f.percentage);
		}
		ss << "\n";
	}

	if (!report.top_lines.empty()) {
		ss << "#### Top Source Lines (Ranked by CPU Cycles)\n\n";
		ss << "| Rank | Source File | Line | Function | Samples | % CPU Cycles |\n";
		ss << "| :---: | :--- | :---: | :--- | :---: | :---: |\n";
		int rank = 1;
		for (const auto &l : report.top_lines) {
			if (rank > limit) break;
			std::string rel_file = fs_utils::make_relative_to_project(l.file_path, wdir);
			std::string func_name = l.function_name.empty() ? "`<unknown>`" : std::format("`{}`", l.function_name);
			std::string file_str = rel_file.empty() ? "`<unknown>`" : std::format("`{}`", rel_file);
			ss << std::format("| {} | {} | {} | {} | {} | **{:.2f}%** |\n",
				rank++, file_str, l.line_number, func_name, l.count, l.percentage);
		}
	}

	set_success(ctx, "Profile summary retrieved.");
	return fs_utils::wrap_prompt_untrusted_data_tag("agent_get_profile_summary_result", ss.str());
}

} // namespace tools
