#include "plugins/verilog/verilog_coverage_report.h"

#include "mapped_file.h"
#include "plugins/verilog/verilog_module_scanner.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace verilog
{

namespace
{

// Parse Verilator S-field line span list, e.g. "132-139,141" -> {132..139, 141}.
void parse_line_spans(std::string_view span_str, std::vector<int> &out_lines)
{
	size_t pos = 0;
	while (pos < span_str.size()) {
		size_t comma = span_str.find(',', pos);
		std::string_view part = (comma == std::string_view::npos) ? span_str.substr(pos) : span_str.substr(pos, comma - pos);
		pos = (comma == std::string_view::npos) ? span_str.size() : (comma + 1);
		if (part.empty()) {
			continue;
		}
		size_t dash = part.find('-');
		try {
			if (dash == std::string_view::npos) {
				int ln = std::stoi(std::string(part));
				if (ln > 0) {
					out_lines.push_back(ln);
				}
			} else {
				int s = std::stoi(std::string(part.substr(0, dash)));
				int e = std::stoi(std::string(part.substr(dash + 1)));
				if (s > 0 && e >= s && (e - s) <= 2000) {
					for (int ln = s; ln <= e; ++ln) {
						out_lines.push_back(ln);
					}
				}
			}
		} catch (...) {
			// Ignore malformed span token
		}
	}
}

std::string trim_sv_line(std::string_view sv)
{
	while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
		sv.remove_suffix(1);
	}
	constexpr size_t kMaxSnippetLen = 110;
	if (sv.size() > kMaxSnippetLen) {
		return std::string(sv.substr(0, kMaxSnippetLen - 3)) + "...";
	}
	return std::string(sv);
}

std::string build_range_snippet(const fs::path &full_path, int start_ln, int end_ln)
{
	std::string err;
	auto mf = mapped_file::get_cached(full_path.string(), err, /*populate=*/false);
	if (!mf) {
		return "";
	}
	auto lines = mf->lines();
	if (start_ln <= 0 || static_cast<size_t>(start_ln) > lines.size()) {
		return "";
	}
	int clamped_end = std::min(end_ln, static_cast<int>(lines.size()));
	std::string first = trim_sv_line(lines[static_cast<size_t>(start_ln) - 1]);
	if (clamped_end <= start_ln) {
		return first;
	}
	std::string last = trim_sv_line(lines[static_cast<size_t>(clamped_end) - 1]);
	if (clamped_end == start_ln + 1) {
		return std::format("{} | {}", first, last);
	}
	return std::format("{}  ...  {}", first, last);
}

} // namespace

std::vector<module_coverage_stats> verilog_coverage_report::parse_and_merge(
	const fs::path &project_root,
	std::span<const std::string> rtl_files,
	std::span<const fs::path> dat_files)
{
	std::error_code ec;
	fs::path canon_root = fs::weakly_canonical(project_root, ec);
	if (ec) {
		canon_root = project_root;
	}

	// Build lookup of allowed RTL files (relative generic path -> canonical full path)
	std::map<std::string, fs::path> allowed_rtl_by_rel;
	std::map<std::string, std::string> canon_to_rel;
	for (const auto &rf : rtl_files) {
		fs::path p(rf);
		fs::path canon = fs::weakly_canonical(p, ec);
		if (ec) {
			canon = p;
			ec.clear();
		}
		fs::path rel = fs::relative(canon, canon_root, ec);
		std::string rel_str = (!ec && !rel.empty()) ? rel.generic_string() : p.generic_string();
		ec.clear();
		allowed_rtl_by_rel[rel_str] = canon;
		canon_to_rel[canon.generic_string()] = rel_str;
	}

	auto resolve_rtl_rel = [&](std::string_view raw_file) -> std::string {
		if (raw_file.empty()) {
			return "";
		}
		fs::path p(raw_file);
		std::string gen = p.generic_string();
		if (allowed_rtl_by_rel.contains(gen)) {
			return gen;
		}
		fs::path full = p.is_absolute() ? p : (canon_root / p);
		fs::path canon = fs::weakly_canonical(full, ec);
		if (!ec) {
			auto it = canon_to_rel.find(canon.generic_string());
			if (it != canon_to_rel.end()) {
				return it->second;
			}
		}
		ec.clear();
		return "";
	};

	struct mod_accum {
		std::string rel_file;
		std::map<int, uint64_t> line_hits;
		// port_name -> {rise_hits, fall_hits}
		std::map<std::string, std::pair<uint64_t, uint64_t>> toggle_hits;
	};
	std::map<std::string, mod_accum> by_mod;

	for (const auto &dat_path : dat_files) {
		std::ifstream in(dat_path, std::ios::binary);
		if (!in.is_open()) {
			continue;
		}
		std::string line;
		while (std::getline(in, line)) {
			if (!line.starts_with("C '")) {
				continue;
			}
			size_t end_q = line.rfind('\'');
			if (end_q == std::string::npos || end_q <= 3) {
				continue;
			}
			uint64_t count = 0;
			try {
				std::string_view cnt_sv(line.data() + end_q + 1, line.size() - end_q - 1);
				while (!cnt_sv.empty() && (cnt_sv.front() == ' ' || cnt_sv.front() == '\t')) {
					cnt_sv.remove_prefix(1);
				}
				if (!cnt_sv.empty()) {
					count = std::stoull(std::string(cnt_sv));
				}
			} catch (...) {
				continue;
			}

			// Parse \x01<key>\x02<val> fields inside quotes
			std::string_view body(line.data() + 3, end_q - 3);
			std::string_view f_file, f_line, f_type, f_page, f_obj, f_spans;
			size_t pos = 0;
			while (pos < body.size()) {
				size_t kstart = body.find('\x01', pos);
				if (kstart == std::string_view::npos) {
					break;
				}
				size_t vsep = body.find('\x02', kstart + 1);
				if (vsep == std::string_view::npos) {
					break;
				}
				size_t next_k = body.find('\x01', vsep + 1);
				std::string_view key = body.substr(kstart + 1, vsep - kstart - 1);
				std::string_view val = (next_k == std::string_view::npos)
					? body.substr(vsep + 1)
					: body.substr(vsep + 1, next_k - vsep - 1);
				pos = (next_k == std::string_view::npos) ? body.size() : next_k;

				if (key == "f") f_file = val;
				else if (key == "l") f_line = val;
				else if (key == "t") f_type = val;
				else if (key == "page") f_page = val;
				else if (key == "o") f_obj = val;
				else if (key == "S") f_spans = val;
			}

			std::string rel_file = resolve_rtl_rel(f_file);
			if (rel_file.empty()) {
				continue; // Exclude tb/ and non-RTL files
			}

			std::string mod_name;
			if (size_t slash = f_page.rfind('/'); slash != std::string_view::npos && slash + 1 < f_page.size()) {
				mod_name = std::string(f_page.substr(slash + 1));
			} else {
				mod_name = fs::path(rel_file).stem().string();
			}

			auto &acc = by_mod[mod_name];
			if (acc.rel_file.empty()) {
				acc.rel_file = rel_file;
			}

			if (f_type == "line") {
				std::vector<int> span_lines;
				if (!f_spans.empty()) {
					parse_line_spans(f_spans, span_lines);
				}
				if (span_lines.empty() && !f_line.empty()) {
					try {
						int ln = std::stoi(std::string(f_line));
						if (ln > 0) span_lines.push_back(ln);
					} catch (...) {}
				}
				for (int ln : span_lines) {
					acc.line_hits[ln] += count;
				}
			} else if (f_type == "toggle" && !f_obj.empty()) {
				bool is_rise = f_obj.ends_with(":0->1");
				bool is_fall = f_obj.ends_with(":1->0");
				if (!is_rise && !is_fall) {
					continue;
				}
				std::string_view sig = f_obj.substr(0, f_obj.size() - 5);
				if (size_t br = sig.find('['); br != std::string_view::npos) {
					sig = sig.substr(0, br);
				}
				if (!sig.empty()) {
					auto &pair = acc.toggle_hits[std::string(sig)];
					if (is_rise) pair.first += count;
					else pair.second += count;
				}
			}
		}
	}

	std::vector<std::string> rtl_vec(rtl_files.begin(), rtl_files.end());
	std::vector<module_coverage_stats> results;
	results.reserve(by_mod.size());

	for (const auto &[mname, acc] : by_mod) {
		module_coverage_stats st;
		st.module_name = mname;
		st.rel_file = acc.rel_file;
		st.total_lines = acc.line_hits.size();

		std::vector<int> missed_lines;
		for (const auto &[ln, hits] : acc.line_hits) {
			if (hits > 0) {
				++st.covered_lines;
			} else {
				missed_lines.push_back(ln);
			}
		}
		st.coverage_pct = (st.total_lines > 0)
			? (100.0 * static_cast<double>(st.covered_lines) / static_cast<double>(st.total_lines))
			: 100.0;

		// Group contiguous uncovered lines into compact ranges with RTL snippets
		fs::path full_path = allowed_rtl_by_rel.contains(acc.rel_file)
			? allowed_rtl_by_rel[acc.rel_file]
			: (canon_root / acc.rel_file);
		for (size_t i = 0; i < missed_lines.size(); ) {
			int r_start = missed_lines[i];
			int r_end = r_start;
			while (i + 1 < missed_lines.size() && missed_lines[i + 1] == r_end + 1) {
				r_end = missed_lines[++i];
			}
			++i;
			st.uncovered_ranges.push_back({
				r_start,
				r_end,
				build_range_snippet(full_path, r_start, r_end)
			});
		}

		// Filter toggle stats to declared module ports only (compact, high-signal)
		auto ports = verilog_module_scanner::extract_module_ports(mname, rtl_vec);
		for (const auto &port : ports) {
			auto tit = acc.toggle_hits.find(port.name);
			if (tit == acc.toggle_hits.end()) {
				continue;
			}
			bool never_rose = (tit->second.first == 0);
			bool never_fell = (tit->second.second == 0);
			if (never_rose || never_fell) {
				st.untoggled_ports.push_back({port.name, never_rose, never_fell});
			}
		}

		results.push_back(std::move(st));
	}

	// Sort lowest coverage first so agents see the highest-priority gaps at the top
	std::sort(results.begin(), results.end(), [](const module_coverage_stats &a, const module_coverage_stats &b) {
		if (std::abs(a.coverage_pct - b.coverage_pct) > 1e-6) {
			return a.coverage_pct < b.coverage_pct;
		}
		return a.module_name < b.module_name;
	});

	return results;
}

void verilog_coverage_report::apply_and_save_deltas(
	const fs::path &state_json_path,
	std::vector<module_coverage_stats> &stats)
{
	std::map<std::string, double> prev_map;
	{
		std::ifstream in(state_json_path, std::ios::binary);
		if (in.is_open()) {
			try {
				nlohmann::json j = nlohmann::json::parse(in);
				if (j.is_object()) {
					for (const auto &[k, v] : j.items()) {
						if (v.is_number()) {
							prev_map[k] = v.get<double>();
						}
					}
				}
			} catch (...) {
				// Ignore corrupt state file
			}
		}
	}

	nlohmann::json next_j = nlohmann::json::object();
	for (auto &st : stats) {
		auto it = prev_map.find(st.module_name);
		if (it != prev_map.end()) {
			st.has_prev_pct = true;
			st.prev_pct = it->second;
		}
		next_j[st.module_name] = st.coverage_pct;
	}

	std::error_code ec;
	if (state_json_path.has_parent_path()) {
		fs::create_directories(state_json_path.parent_path(), ec);
	}
	std::ofstream out(state_json_path, std::ios::binary | std::ios::trunc);
	if (out.is_open()) {
		out << next_j.dump(2) << "\n";
	}
}

std::string verilog_coverage_report::format_summary_markdown(
	std::span<const module_coverage_stats> stats,
	size_t merged_tb_count,
	bool include_vfs_pointer)
{
	std::ostringstream out;
	out << std::format("\n### RTL Test Coverage Summary (merged {} testbench{})\n\n",
	                   merged_tb_count, merged_tb_count == 1 ? "" : "es");
	if (stats.empty()) {
		out << "_No RTL coverage points recorded._\n";
		return out.str();
	}

	out << "| Module | File | Line Coverage | Uncovered | Untoggled Ports |\n";
	out << "|--------|------|--------------:|----------:|-----------------|\n";

	size_t tot_cov = 0;
	size_t tot_all = 0;
	for (const auto &st : stats) {
		tot_cov += st.covered_lines;
		tot_all += st.total_lines;
		size_t uncov = st.total_lines - st.covered_lines;

		std::string cov_str = std::format("{:.1f}% ({}/{})", st.coverage_pct, st.covered_lines, st.total_lines);
		if (st.has_prev_pct && std::abs(st.coverage_pct - st.prev_pct) >= 0.05) {
			double delta = st.coverage_pct - st.prev_pct;
			cov_str += std::format(" ({:+.1f}%)", delta);
		}

		std::string ports_str = "-";
		if (!st.untoggled_ports.empty()) {
			ports_str.clear();
			size_t show = std::min<size_t>(st.untoggled_ports.size(), 3);
			for (size_t i = 0; i < show; ++i) {
				if (i > 0) ports_str += ", ";
				const auto &p = st.untoggled_ports[i];
				std::string state = (p.never_rose && p.never_fell) ? "never toggled"
					: (p.never_rose ? "never 1" : "never 0");
				ports_str += std::format("`{}` ({})", p.port_name, state);
			}
			if (st.untoggled_ports.size() > show) {
				ports_str += std::format(" (+{} more)", st.untoggled_ports.size() - show);
			}
		}

		out << std::format("| `{}` | `{}` | {} | {} | {} |\n",
		                   st.module_name, st.rel_file, cov_str, uncov, ports_str);
	}

	if (stats.size() > 1 && tot_all > 0) {
		double tot_pct = 100.0 * static_cast<double>(tot_cov) / static_cast<double>(tot_all);
		out << std::format("| **Total** | | **{:.1f}% ({}/{})** | **{}** | |\n",
		                   tot_pct, tot_cov, tot_all, tot_all - tot_cov);
	}

	if (include_vfs_pointer) {
		out << "\n> [!TIP] Detailed uncovered line ranges & RTL snippets are available at `system://verilog/coverage.md` "
		       "(read with `fs_read_lines(path=\"system://verilog/coverage.md\")` or filter with `?search=<module>`).\n";
	}
	return out.str();
}

std::string verilog_coverage_report::format_detailed_markdown(
	std::span<const module_coverage_stats> stats,
	size_t merged_tb_count)
{
	std::ostringstream out;
	out << std::format("# Verilog RTL Test Coverage Report (merged {} testbench{})\n\n",
	                   merged_tb_count, merged_tb_count == 1 ? "" : "es");
	if (stats.empty()) {
		out << "_No RTL coverage data recorded._\n";
		return out.str();
	}

	for (const auto &st : stats) {
		size_t uncov = st.total_lines - st.covered_lines;
		out << std::format("## Module `{}` (`{}`) — {:.1f}% ({}/{} lines, {} uncovered)\n\n",
		                   st.module_name, st.rel_file, st.coverage_pct, st.covered_lines, st.total_lines, uncov);

		if (st.uncovered_ranges.empty()) {
			out << "- All instrumented lines covered.\n";
		} else {
			out << "### Uncovered Line Ranges\n";
			for (const auto &r : st.uncovered_ranges) {
				std::string loc = (r.start_line == r.end_line)
					? std::format("L{}", r.start_line)
					: std::format("L{}-L{}", r.start_line, r.end_line);
				if (!r.snippet.empty()) {
					out << std::format("- `{}`: `{}`\n", loc, r.snippet);
				} else {
					out << std::format("- `{}`\n", loc);
				}
			}
		}

		if (!st.untoggled_ports.empty()) {
			out << "\n### Untoggled Ports\n";
			for (const auto &p : st.untoggled_ports) {
				std::string state = (p.never_rose && p.never_fell) ? "never toggled"
					: (p.never_rose ? "stuck 0 (never rose 0->1)" : "stuck 1 (never fell 1->0)");
				out << std::format("- `{}`: {}\n", p.port_name, state);
			}
		}
		out << "\n";
	}
	return out.str();
}

bool verilog_coverage_report::write_report_file(const fs::path &report_path, std::string_view markdown)
{
	std::error_code ec;
	if (report_path.has_parent_path()) {
		fs::create_directories(report_path.parent_path(), ec);
	}
	std::ofstream out(report_path, std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out.write(markdown.data(), static_cast<std::streamsize>(markdown.size()));
	return out.good();
}

} // namespace verilog
