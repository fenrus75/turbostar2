#include "agent_get_profile_details.h"
#include "../../fs_utils.h"
#include "../../lsp_manager.h"
#include "../../perf_manager.h"
#include "../../project_manager.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace tools
{

namespace
{

struct line_range {
	int start_line;
	int end_line;
};

struct symbol_bounds {
	int start_line{0};
	int end_line{0};
};

static bool find_matching_symbol(const lsp_manager::symbol_node &node, const std::string &target_name,
				  int target_line, lsp_manager::symbol_node &out_match)
{
	auto match_string = [](std::string_view target, std::string_view query) -> bool {
		if (target.empty() || query.empty()) return false;
		std::string t, q;
		for (char c : target) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		for (char c : query) q.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		return t.find(q) != std::string::npos || q.find(t) != std::string::npos;
	};

	int start_l = node.range.start_y + 1;
	int end_l = node.range.end_y + 1;

	bool name_matches = !target_name.empty() && match_string(node.name, target_name);
	bool line_contains = (target_line > 0 && target_line >= start_l && target_line <= end_l);

	if (name_matches || line_contains) {
		for (const auto &child : node.children) {
			if (find_matching_symbol(child, target_name, target_line, out_match)) {
				return true;
			}
		}
		out_match = node;
		return true;
	}

	for (const auto &child : node.children) {
		if (find_matching_symbol(child, target_name, target_line, out_match)) {
			return true;
		}
	}
	return false;
}

static symbol_bounds query_lsp_symbol_bounds(const std::string &abs_file_path, const std::string &target_func,
					      int target_line)
{
	symbol_bounds bounds{0, 0};
	if (abs_file_path.empty()) {
		return bounds;
	}

	auto symbols = project_manager::get_instance().lsp_query_document_symbols(abs_file_path);
	if (symbols.empty()) {
		return bounds;
	}

	lsp_manager::symbol_node match;
	for (const auto &root : symbols) {
		if (find_matching_symbol(root, target_func, target_line, match)) {
			bounds.start_line = match.range.start_y + 1;
			bounds.end_line = match.range.end_y + 1;
			break;
		}
	}
	return bounds;
}

static std::vector<line_range> merge_line_ranges(const std::vector<int> &hot_lines, int total_lines,
						  const symbol_bounds &bounds)
{
	int func_start = (bounds.start_line > 0) ? bounds.start_line : 1;
	int func_end = (bounds.end_line > 0) ? bounds.end_line : total_lines;

	std::vector<line_range> ranges;
	for (int line : hot_lines) {
		int start = std::max(func_start, line - 2);
		int end = (func_end > 0) ? std::min(func_end, line + 2) : line + 2;
		ranges.push_back({start, end});
	}

	if (bounds.start_line > 0 && bounds.end_line > 0) {
		ranges.push_back({bounds.start_line, bounds.start_line});
		ranges.push_back({bounds.end_line, bounds.end_line});
	}

	std::sort(ranges.begin(), ranges.end(), [](const line_range &a, const line_range &b) {
		return a.start_line < b.start_line;
	});

	std::vector<line_range> merged;
	for (const auto &r : ranges) {
		if (merged.empty()) {
			merged.push_back(r);
		} else {
			if (r.start_line <= merged.back().end_line + 1) {
				merged.back().end_line = std::max(merged.back().end_line, r.end_line);
			} else {
				merged.push_back(r);
			}
		}
	}
	return merged;
}

static std::string resolve_file_path(const std::string &file_path, const agentlib::tool_context &ctx)
{
	if (file_path.empty() || file_path == "??") {
		return "";
	}

	std::vector<std::string> candidates;
	if (std::filesystem::path(file_path).is_absolute()) {
		candidates.push_back(file_path);
	} else {
		std::string wdir = ctx.fs_security.get_working_directory();
		if (!wdir.empty()) {
			candidates.push_back((std::filesystem::path(wdir) / file_path).string());
		}
		std::string proj_root = project_manager::get_instance().get_project_root();
		if (!proj_root.empty()) {
			candidates.push_back((std::filesystem::path(proj_root) / file_path).string());
		}
		std::string proj_dir = fs_utils::get_project_dir();
		if (!proj_dir.empty()) {
			candidates.push_back((std::filesystem::path(proj_dir) / file_path).string());
		}
		candidates.push_back(file_path);
	}

	for (const auto &cand : candidates) {
		std::error_code ec;
		if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec)) {
			return cand;
		}
	}
	return file_path;
}

static std::vector<std::string> read_file_lines(const std::string &resolved_path)
{
	std::vector<std::string> lines;
	if (resolved_path.empty()) {
		return lines;
	}
	std::ifstream in(resolved_path);
	if (!in.is_open()) {
		return lines;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		lines.push_back(line);
	}
	return lines;
}

} // namespace

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
		for (char c : target) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		std::string q;
		q.reserve(query.size());
		for (char c : query) q.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		return t.find(q) != std::string::npos || q.find(t) != std::string::npos;
	};

	// 1. Filter raw line samples based on file_path or function_name
	std::vector<turbostar::perf_line_sample> matched_samples;

	if (!args_.file_path.empty()) {
		for (const auto &l : report.top_lines) {
			if (match_string(l.file_path, args_.file_path)) {
				matched_samples.push_back(l);
			}
		}
	} else if (!args_.function_name.empty()) {
		for (const auto &l : report.top_lines) {
			if (match_string(l.function_name, args_.function_name)) {
				matched_samples.push_back(l);
			}
		}
		if (matched_samples.empty()) {
			for (const auto &f : report.top_functions) {
				if (match_string(f.function_name, args_.function_name) && !f.file_path.empty()) {
					auto it = report.line_samples_by_file.find(f.file_path);
					if (it != report.line_samples_by_file.end()) {
						for (const auto &ls : it->second) {
							matched_samples.push_back(ls);
						}
					}
				}
			}
		}
	} else {
		matched_samples = report.top_lines;
	}

	uint64_t target_total_samples = 0;
	for (const auto &s : matched_samples) {
		target_total_samples += s.count;
	}

	// Group samples by file_path
	std::unordered_map<std::string, std::map<int, turbostar::perf_line_sample>> samples_by_file;
	for (const auto &s : matched_samples) {
		samples_by_file[s.file_path][s.line_number] = s;
	}

	nlohmann::json line_samples = nlohmann::json::array();
	std::string primary_file_path;

	for (const auto &file_pair : samples_by_file) {
		const std::string &file_path = file_pair.first;
		if (primary_file_path.empty()) {
			primary_file_path = file_path;
		}

		const auto &line_map = file_pair.second;
		std::vector<int> hot_line_numbers;
		for (const auto &lp : line_map) {
			hot_line_numbers.push_back(lp.first);
		}

		std::string resolved_path = resolve_file_path(file_path, ctx);
		auto file_lines = read_file_lines(resolved_path);
		int total_lines = static_cast<int>(file_lines.size());

		int representative_line = hot_line_numbers.empty() ? 0 : hot_line_numbers.front();
		symbol_bounds bounds = query_lsp_symbol_bounds(resolved_path, args_.function_name, representative_line);

		std::vector<line_range> ranges = merge_line_ranges(hot_line_numbers, total_lines, bounds);

		for (const auto &r : ranges) {
			for (int line_num = r.start_line; line_num <= r.end_line; ++line_num) {
				nlohmann::json entry;
				entry["line_number"] = line_num;

				if (line_num >= 1 && line_num <= total_lines) {
					entry["code"] = file_lines[line_num - 1];
				} else {
					entry["code"] = nullptr;
				}

				auto it = line_map.find(line_num);
				if (it != line_map.end()) {
					const auto &s = it->second;
					double global_pct = (report.total_samples > 0)
								? (static_cast<double>(s.count) * 100.0 / report.total_samples)
								: 0.0;
					double func_pct = (target_total_samples > 0)
							      ? (static_cast<double>(s.count) * 100.0 / target_total_samples)
							      : 0.0;
					entry["count"] = s.count;
					entry["global_percentage"] = global_pct;
					entry["function_percentage"] = func_pct;
					if (!s.function_name.empty()) {
						entry["function_name"] = s.function_name;
					}
				} else {
					entry["count"] = 0;
					entry["global_percentage"] = 0.0;
					entry["function_percentage"] = 0.0;
				}

				if (samples_by_file.size() > 1) {
					entry["file_path"] = file_path;
				}
				line_samples.push_back(entry);
			}
		}
	}

	nlohmann::json output = {
	    {"total_samples", report.total_samples},
	    {"target_samples", target_total_samples},
	    {"file_path", primary_file_path.empty() ? (args_.file_path.empty() ? nullptr : nlohmann::json(args_.file_path)) : nlohmann::json(primary_file_path)},
	    {"function_name", args_.function_name.empty() ? nullptr : nlohmann::json(args_.function_name)},
	    {"line_samples", line_samples},
	};

	set_success(ctx, "Retrieved profile details (" + std::to_string(line_samples.size()) + " line entries)");
	return output.dump(2);
}

} // namespace tools
