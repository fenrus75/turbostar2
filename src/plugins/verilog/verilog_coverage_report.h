#pragma once

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace verilog
{

struct uncovered_line_range {
	int start_line{0};
	int end_line{0};
	std::string snippet; // Single line, or start + "..." + end for multi-line spans
};

struct untoggled_port_info {
	std::string port_name;
	bool never_rose{false}; // 0->1 count == 0
	bool never_fell{false}; // 1->0 count == 0
};

struct module_coverage_stats {
	std::string module_name;
	std::string rel_file;
	size_t covered_lines{0};
	size_t total_lines{0};
	double coverage_pct{0.0};
	bool has_prev_pct{false};
	double prev_pct{0.0};
	std::vector<uncovered_line_range> uncovered_ranges;
	std::vector<untoggled_port_info> untoggled_ports;
};

/**
 * @brief Parses and merges Verilator coverage.dat files across multiple testbenches,
 * filters to RTL files only, computes per-module line coverage and port toggle gaps,
 * and formats both compact summary tables and detailed system://verilog/coverage.md reports.
 */
class verilog_coverage_report {
public:
	// Parse and merge one or more Verilator .dat files, restricted to project RTL files (excluding tb/).
	static std::vector<module_coverage_stats> parse_and_merge(
		const std::filesystem::path &project_root,
		std::span<const std::string> rtl_files,
		std::span<const std::filesystem::path> dat_files);

	// Load previous per-module percentages from <tmp_dir>/verilog_coverage_last.json, attach deltas,
	// and persist the current percentages for the next run.
	static void apply_and_save_deltas(
		const std::filesystem::path &state_json_path,
		std::vector<module_coverage_stats> &stats);

	// Format compact per-module coverage table for inline tool / CLI output.
	static std::string format_summary_markdown(
		std::span<const module_coverage_stats> stats,
		size_t merged_tb_count,
		bool include_vfs_pointer);

	// Format detailed per-module uncovered line ranges + RTL snippets + untoggled ports
	// for system://verilog/coverage.md (or CLI detailed output).
	static std::string format_detailed_markdown(
		std::span<const module_coverage_stats> stats,
		size_t merged_tb_count);

	// Persist the detailed report to <project_tmp>/verilog_coverage.md so system://verilog/coverage.md serves it.
	static bool write_report_file(const std::filesystem::path &report_path, std::string_view markdown);
};

} // namespace verilog
