#include "agent_get_profile_details.h"
#include "../../config_manager.h"
#include "../../fs_utils.h"
#include "../../lsp_manager.h"
#include "../../perf_manager.h"
#include "../../project_manager.h"
#include "codemap_utils.h"
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

static bool match_symbol_string(std::string_view target, std::string_view query)
{
	if (target.empty() || query.empty()) {
		return false;
	}
	std::string t;
	t.reserve(target.size());
	for (char c : target) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	std::string q;
	q.reserve(query.size());
	for (char c : query) q.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

	if (t.find(q) != std::string::npos || q.find(t) != std::string::npos) {
		return true;
	}

	auto strip_params = [](const std::string &s) -> std::string {
		size_t paren = s.find('(');
		if (paren != std::string::npos) {
			std::string base = s.substr(0, paren);
			while (!base.empty() && std::isspace(static_cast<unsigned char>(base.back()))) {
				base.pop_back();
			}
			return base;
		}
		return s;
	};

	std::string t_base = strip_params(t);
	std::string q_base = strip_params(q);
	if (!t_base.empty() && !q_base.empty()) {
		if (t_base.find(q_base) != std::string::npos || q_base.find(t_base) != std::string::npos) {
			return true;
		}
	}

	return false;
}

static bool find_matching_symbol(const lsp_manager::symbol_node &node, const std::string &target_name,
				  int target_line, lsp_manager::symbol_node &out_match)
{
	int start_l = node.range.start_y + 1;
	int end_l = node.range.end_y + 1;

	bool name_matches = !target_name.empty() && match_symbol_string(node.name, target_name);
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



static symbol_bounds tighten_symbol_bounds(symbol_bounds bounds, const std::vector<std::string> &file_lines)
{

	int start_idx = std::max(0, bounds.start_line - 1);
	int end_idx = std::min(static_cast<int>(file_lines.size()) - 1, bounds.end_line - 1);

	auto is_comment_or_blank = [](const std::string &line) -> bool {
		size_t first = line.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) {
			return true;
		}
		if (line.compare(first, 2, "//") == 0) {
			return true;
		}
		if (line.compare(first, 2, "/*") == 0) {
			return true;
		}
		return false;
	};

	while (end_idx > start_idx && is_comment_or_blank(file_lines[end_idx])) {
		--end_idx;
	}

	bounds.end_line = end_idx + 1;
	return bounds;
}

static std::vector<line_range> merge_line_ranges(const std::vector<int> &hot_lines, int total_lines,
						  const symbol_bounds &bounds)
{
	int func_start = (bounds.start_line > 0) ? bounds.start_line : 1;
	int func_end = (bounds.end_line > 0) ? bounds.end_line : total_lines;

	std::vector<line_range> ranges;
	for (int line : hot_lines) {
		int start = std::max(1, line - 2);
		int end = (total_lines > 0) ? std::min(total_lines, line + 2) : line + 2;

		if (func_start > 0 && func_end >= func_start) {
			if (line >= func_start - 2 && line <= func_end + 2) {
				start = std::max(func_start, start);
				end = std::min(func_end, end);
			}
		}
		if (start <= end) {
			ranges.push_back({start, end});
		}
	}

	if (bounds.start_line > 0 && bounds.end_line >= bounds.start_line) {
		ranges.push_back({bounds.start_line, bounds.start_line});
		ranges.push_back({bounds.end_line, bounds.end_line});
	}

	std::sort(ranges.begin(), ranges.end(), [](const line_range &a, const line_range &b) {
		return a.start_line < b.start_line;
	});

	std::vector<line_range> merged;
	for (const auto &r : ranges) {
		if (r.start_line > r.end_line) {
			continue;
		}
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

static std::string resolve_file_path(const std::string &input_path, const agentlib::tool_context &ctx)
{
	if (input_path.empty() || input_path == "??") {
		return "";
	}

	std::string clean_path = input_path;
	size_t colon_idx = clean_path.rfind(':');
	if (colon_idx != std::string::npos && colon_idx + 1 < clean_path.size()) {
		bool all_digits = true;
		for (size_t k = colon_idx + 1; k < clean_path.size(); ++k) {
			if (!std::isdigit(static_cast<unsigned char>(clean_path[k]))) {
				all_digits = false;
				break;
			}
		}
		if (all_digits) {
			clean_path = clean_path.substr(0, colon_idx);
		}
	}

	std::string norm_input = std::filesystem::path(clean_path).lexically_normal().string();
	std::vector<std::string> raw_paths = {norm_input, clean_path};

	std::string norm = norm_input;
	while (norm.starts_with("../") || norm.starts_with("./")) {
		if (norm.starts_with("../")) {
			norm = norm.substr(3);
		} else if (norm.starts_with("./")) {
			norm = norm.substr(2);
		}
	}
	if (norm != norm_input && norm != clean_path) {
		raw_paths.push_back(norm);
	}

	std::vector<std::filesystem::path> base_dirs;
	std::string proj_root = project_manager::get_instance().get_project_root();
	std::string proj_dir = fs_utils::get_project_dir();
	std::string wdir = ctx.fs_security.get_working_directory();
	std::string cfg_build_dir = config_manager::get_instance().get_build_directory();

	// 1. Prioritize configured build directory from config_manager
	if (!cfg_build_dir.empty()) {
		if (std::filesystem::path(cfg_build_dir).is_absolute()) {
			base_dirs.push_back(cfg_build_dir);
		} else {
			if (!proj_root.empty()) {
				base_dirs.push_back(std::filesystem::path(proj_root) / cfg_build_dir);
			}
			if (!proj_dir.empty()) {
				base_dirs.push_back(std::filesystem::path(proj_dir) / cfg_build_dir);
			}
			if (!wdir.empty()) {
				base_dirs.push_back(std::filesystem::path(wdir) / cfg_build_dir);
			}
		}
	}

	// 2. Fallback build directory candidates (where DWARF ../ relative paths originate)
	if (!proj_root.empty()) {
		base_dirs.push_back(std::filesystem::path(proj_root) / "build");
		base_dirs.push_back(std::filesystem::path(proj_root) / "builddir");
		base_dirs.push_back(std::filesystem::path(proj_root) / "out");
		base_dirs.push_back(std::filesystem::path(proj_root) / "cmake-build-debug");
		base_dirs.push_back(std::filesystem::path(proj_root));
	}
	if (!proj_dir.empty()) {
		base_dirs.push_back(std::filesystem::path(proj_dir) / "build");
		base_dirs.push_back(std::filesystem::path(proj_dir) / "builddir");
		base_dirs.push_back(std::filesystem::path(proj_dir));
	}
	if (!wdir.empty()) {
		base_dirs.push_back(std::filesystem::path(wdir) / "build");
		base_dirs.push_back(std::filesystem::path(wdir));
	}

	std::vector<std::string> candidates;
	for (const auto &rp : raw_paths) {
		if (std::filesystem::path(rp).is_absolute()) {
			candidates.push_back(rp);
		} else {
			for (const auto &bdir : base_dirs) {
				candidates.push_back((bdir / rp).string());
			}
			candidates.push_back(rp);
		}
	}

	for (const auto &cand : candidates) {
		std::error_code ec;
		std::filesystem::path p(cand);
		std::filesystem::path norm_p = p.lexically_normal();
		std::string safe_path, err;
		if (std::filesystem::exists(norm_p, ec) && !std::filesystem::is_directory(norm_p, ec)) {
			if (ctx.fs_security.validate_access(norm_p.string(), agentlib::access_type::read, safe_path, err)) {
				return safe_path;
			}
		}
		if (std::filesystem::exists(cand, ec) && !std::filesystem::is_directory(cand, ec)) {
			if (ctx.fs_security.validate_access(cand, agentlib::access_type::read, safe_path, err)) {
				return safe_path;
			}
		}
	}

	// 2. Final fallback: Recursive scan of project root matching target filename
	std::string target_fname = std::filesystem::path(norm_input).filename().string();
	if (!target_fname.empty() && target_fname != "??" && !proj_root.empty()) {
		std::error_code ec;
		for (const auto &entry : std::filesystem::recursive_directory_iterator(proj_root, ec)) {
			if (ec) break;
			if (entry.is_regular_file() && entry.path().filename().string() == target_fname) {
				std::string safe_path, err;
				if (ctx.fs_security.validate_access(entry.path().string(), agentlib::access_type::read, safe_path, err)) {
					return safe_path;
				}
			}
		}
	}

	return norm_input;
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

static std::string make_relative_to_project(const std::string &path_str, const agentlib::tool_context &ctx)
{
	return fs_utils::make_relative_to_project(path_str, ctx.fs_security.get_working_directory().native());
}

} // namespace

bool agent_get_profile_details_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string agent_get_profile_details_tool::execute(agentlib::tool_context &ctx)
{
	auto report = turbostar::perf_manager::get_instance().get_profile_for_run(args_.run_id);
	if (report.total_samples == 0 && args_.run_id.empty()) {
		std::string perf_dir = fs_utils::get_project_perf_dir();
		report = turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir, 0, "editor", true);
	}

	if (report.total_samples == 0) {
		set_success(ctx, "No performance profile samples collected.");
		return nlohmann::json{{"run_id", args_.run_id.empty() ? "editor" : args_.run_id},
				      {"total_samples", 0},
				      {"line_samples", nlohmann::json::array()}}
		    .dump(2);
	}

	// 1. Filter raw line samples based on file_path or function_name
	std::vector<turbostar::perf_line_sample> matched_samples;

	if (!args_.file_path.empty()) {
		for (const auto &l : report.top_lines) {
			std::string res_path = resolve_file_path(l.file_path, ctx);
			std::string target_res_path = resolve_file_path(args_.file_path, ctx);
			if (match_symbol_string(l.file_path, args_.file_path) ||
			    (!res_path.empty() && res_path == target_res_path)) {
				matched_samples.push_back(l);
			}
		}
		if (matched_samples.empty()) {
			for (const auto &pair : report.line_samples_by_file) {
				std::string res_path = resolve_file_path(pair.first, ctx);
				std::string target_res_path = resolve_file_path(args_.file_path, ctx);
				if (match_symbol_string(pair.first, args_.file_path) ||
				    (!res_path.empty() && res_path == target_res_path)) {
					for (const auto &ls : pair.second) {
						matched_samples.push_back(ls);
					}
				}
			}
		}
	} else if (!args_.function_name.empty()) {
		for (const auto &l : report.top_lines) {
			if (match_symbol_string(l.function_name, args_.function_name)) {
				matched_samples.push_back(l);
			}
		}
		for (const auto &f : report.top_functions) {
			if (match_symbol_string(f.function_name, args_.function_name)) {
				if (!f.file_path.empty()) {
					for (const auto &pair : report.line_samples_by_file) {
						std::string res_key = resolve_file_path(pair.first, ctx);
						std::string res_func = resolve_file_path(f.file_path, ctx);
						if (pair.first == f.file_path ||
						    match_symbol_string(pair.first, f.file_path) ||
						    (!res_key.empty() && res_key == res_func)) {
							for (const auto &ls : pair.second) {
								if (match_symbol_string(ls.function_name, args_.function_name) ||
								    match_symbol_string(ls.function_name, f.function_name)) {
									if (std::find_if(matched_samples.begin(), matched_samples.end(),
											 [&](const turbostar::perf_line_sample &existing) {
												 return existing.file_path == ls.file_path &&
													existing.line_number == ls.line_number;
											 }) == matched_samples.end()) {
										matched_samples.push_back(ls);
									}
								}
							}
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
		std::string rel_file_path = make_relative_to_project(resolved_path.empty() ? file_path : resolved_path, ctx);

		auto file_lines = read_file_lines(resolved_path);
		int total_lines = static_cast<int>(file_lines.size());

		int representative_line = hot_line_numbers.empty() ? 0 : hot_line_numbers.front();
		auto doc_symbols = tools::get_document_codemap_symbols(resolved_path, ctx, 1);
		symbol_bounds bounds{0, 0};

		if (!args_.function_name.empty()) {
			const auto *sym = tools::find_symbol_by_hint(doc_symbols, args_.function_name);
			if (sym) {
				bounds.start_line = sym->start_line;
				bounds.end_line = sym->end_line;
			}
		}
		if (bounds.start_line <= 0 && representative_line > 0) {
			const auto *sym = tools::find_enclosing_symbol(doc_symbols, representative_line);
			if (sym) {
				bounds.start_line = sym->start_line;
				bounds.end_line = sym->end_line;
			}
		}
		bounds = tighten_symbol_bounds(bounds, file_lines);

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
					double target_pct = (target_total_samples > 0)
							      ? (static_cast<double>(s.count) * 100.0 / target_total_samples)
							      : 0.0;
					entry["count"] = s.count;
					entry["global_percentage"] = global_pct;
					if (!args_.file_path.empty()) {
						entry["file_percentage"] = target_pct;
					} else {
						entry["function_percentage"] = target_pct;
					}
					if (!s.function_name.empty()) {
						entry["function_name"] = s.function_name;
					}
				} else {
					entry["count"] = 0;
					entry["global_percentage"] = 0.0;
					if (!args_.file_path.empty()) {
						entry["file_percentage"] = 0.0;
					} else {
						entry["function_percentage"] = 0.0;
					}
				}

				if (samples_by_file.size() > 1) {
					entry["file_path"] = rel_file_path;
				}
				line_samples.push_back(entry);
				if (line_samples.size() >= 200) {
					break;
				}
			}
			if (line_samples.size() >= 200) {
				break;
			}
		}
	}

	std::string display_file_path;
	if (!primary_file_path.empty()) {
		std::string resolved_primary = resolve_file_path(primary_file_path, ctx);
		display_file_path = make_relative_to_project(resolved_primary.empty() ? primary_file_path : resolved_primary, ctx);
	} else if (!args_.file_path.empty()) {
		display_file_path = make_relative_to_project(args_.file_path, ctx);
	}

	nlohmann::json output = {
	    {"run_id", args_.run_id.empty() ? "editor" : args_.run_id},
	    {"total_samples", report.total_samples},
	    {"target_samples", target_total_samples},
	    {"file_path", display_file_path.empty() ? nullptr : nlohmann::json(display_file_path)},
	    {"function_name", args_.function_name.empty() ? nullptr : nlohmann::json(args_.function_name)},
	    {"line_samples", line_samples},
	};

	set_success(ctx, "Retrieved profile details (" + std::to_string(line_samples.size()) + " line entries)");
	return fs_utils::wrap_prompt_untrusted_data_tag("agent_profile_details_result", output.dump(2));
}

} // namespace tools
