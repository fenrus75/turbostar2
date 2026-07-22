#include "agent_get_profile_details.h"
#include "../../fs_utils.h"
#include "../../perf_manager.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_get_profile_details_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string agent_get_profile_details_tool::execute(agentlib::tool_context &ctx)
{
	auto report = turbostar::perf_manager::get_instance().get_active_profile();
	if (report.total_samples == 0) {
		std::string perf_dir = fs_utils::get_project_perf_dir();
		report = turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir, 0, true);
	}

	if (report.total_samples == 0) {
		set_success(ctx, "No performance profile samples collected.");
		return nlohmann::json{{"total_samples", 0}, {"line_samples", nlohmann::json::array()}}.dump(2);
	}

	auto match_string = [](std::string_view target, std::string_view query) -> bool {
		if (target.empty() || query.empty()) {
			return false;
		}
		std::string t;
		t.reserve(target.size());
		for (char c : target) t.push_back(static_cast<char>(std::tolower(c)));
		std::string q;
		q.reserve(query.size());
		for (char c : query) q.push_back(static_cast<char>(std::tolower(c)));
		return t.find(q) != std::string::npos || q.find(t) != std::string::npos;
	};

	nlohmann::json line_samples = nlohmann::json::array();

	if (!args_.file_path.empty()) {
		for (const auto &l : report.top_lines) {
			if (match_string(l.file_path, args_.file_path)) {
				line_samples.push_back({
				    {"file_path", l.file_path},
				    {"line_number", l.line_number},
				    {"function_name", l.function_name.empty() ? nullptr : nlohmann::json(l.function_name)},
				    {"count", l.count},
				    {"percentage", l.percentage},
				});
			}
		}
	} else if (!args_.function_name.empty()) {
		for (const auto &l : report.top_lines) {
			if (match_string(l.function_name, args_.function_name)) {
				line_samples.push_back({
				    {"file_path", l.file_path},
				    {"line_number", l.line_number},
				    {"function_name", l.function_name.empty() ? nullptr : nlohmann::json(l.function_name)},
				    {"count", l.count},
				    {"percentage", l.percentage},
				});
			}
		}
		// Fallback: If no line sample matched directly on ls.function_name, match top_functions to file_path
		if (line_samples.empty()) {
			for (const auto &f : report.top_functions) {
				if (match_string(f.function_name, args_.function_name) && !f.file_path.empty()) {
					auto it = report.line_samples_by_file.find(f.file_path);
					if (it != report.line_samples_by_file.end()) {
						for (const auto &ls : it->second) {
							line_samples.push_back({
							    {"file_path", ls.file_path},
							    {"line_number", ls.line_number},
							    {"function_name", ls.function_name.empty() ? nullptr : nlohmann::json(ls.function_name)},
							    {"count", ls.count},
							    {"percentage", ls.percentage},
							});
						}
					}
				}
			}
		}
	} else {
		for (const auto &l : report.top_lines) {
			line_samples.push_back({
			    {"file_path", l.file_path},
			    {"line_number", l.line_number},
			    {"function_name", l.function_name.empty() ? nullptr : nlohmann::json(l.function_name)},
			    {"count", l.count},
			    {"percentage", l.percentage},
			});
		}
	}

	nlohmann::json output = {
	    {"total_samples", report.total_samples},
	    {"file_path", args_.file_path.empty() ? nullptr : nlohmann::json(args_.file_path)},
	    {"function_name", args_.function_name.empty() ? nullptr : nlohmann::json(args_.function_name)},
	    {"line_samples", line_samples},
	};

	set_success(ctx, "Retrieved profile details (" + std::to_string(line_samples.size()) + " line entries)");
	return output.dump(2);
}

} // namespace tools
