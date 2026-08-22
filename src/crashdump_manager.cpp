#include "crashdump_manager.h"
#include "address_lookup.h"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <tuple>
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "codemap_utils.h"

namespace fs = std::filesystem;

std::string crashdump_info::to_markdown_row() const
{
	std::string cookie_str = crash_cookie.empty() ? "-" : crash_cookie;
	return std::format("| {} | {} | `{}` | {} | {} |", crash_id, timestamp, executable, signal, cookie_str);
}

crashdump_manager &crashdump_manager::get_instance()
{
	static crashdump_manager instance;
	return instance;
}

struct memory_map {
	uint64_t start;
	uint64_t end;
	std::string permissions;
	uint64_t offset;
	std::string dev;
	uint64_t inode;
	std::string path;
};

[[maybe_unused]] static bool check_eu_addr2line_installed()
{
	static bool checked = false;
	static bool installed = false;
	if (!checked) {
		installed = fs::exists("/usr/bin/eu-addr2line");
		checked = true;
	}
	return installed;
}

static std::vector<memory_map> parse_maps(const std::string &maps_file)
{
	std::vector<memory_map> maps;
	std::ifstream in(maps_file);
	std::string line;
	while (std::getline(in, line)) {
		std::istringstream iss(line);
		std::string range;
		memory_map m;
		if (iss >> range >> m.permissions >> std::hex >> m.offset >> std::dec >> m.dev >> m.inode) {
			size_t dash = range.find('-');
			if (dash != std::string::npos) {
				m.start = std::stoull(range.substr(0, dash), nullptr, 16);
				m.end = std::stoull(range.substr(dash + 1), nullptr, 16);
			}
			std::string path;
			std::getline(iss, path);
			size_t first = path.find_first_not_of(" \t");
			if (first != std::string::npos) {
				m.path = path.substr(first);
			}
			maps.push_back(m);
		}
	}
	return maps;
}

static std::string extract_executable_name(const std::vector<memory_map> &maps)
{
	for (const auto &m : maps) {
		if (m.offset == 0 && m.permissions.find('x') != std::string::npos && !m.path.empty() && m.path[0] == '/') {
			if (m.path.find("/lib/") != std::string::npos || m.path.find("/usr/lib") != std::string::npos ||
			    m.path.find("ld-linux") != std::string::npos) {
				continue;
			}
			return fs::path(m.path).filename().string();
		}
	}
	return "App";
}

void crashdump_manager::generate_report_if_needed(std::string_view crash_dir) const
{
	fs::path report_path = fs::path(crash_dir) / "report.md";
	if (fs::exists(report_path))
		return;

	// Set DEBUGINFOD_URLS environment variable for eu-addr2line on demand lookup
	setenv("DEBUGINFOD_URLS", "https://debuginfod.debian.net", 0);

	fs::path maps_path = fs::path(crash_dir) / "maps.txt";
	fs::path stack_path = fs::path(crash_dir) / "stack.bin";
	fs::path info_path = fs::path(crash_dir) / "info.txt";

	if (!fs::exists(maps_path) || !fs::exists(stack_path))
		return;

	auto maps = parse_maps(maps_path.string());
	std::sort(maps.begin(), maps.end(), [](const memory_map &a, const memory_map &b) { return a.start < b.start; });

	std::string info_content;
	std::ifstream info_in(info_path);
	if (info_in) {
		std::ostringstream ss;
		ss << info_in.rdbuf();
		info_content = ss.str();
	}

	fs::path assert_path = fs::path(crash_dir) / "assertion.txt";
	std::string assert_content;
	std::ifstream assert_in(assert_path);
	if (assert_in) {
		std::ostringstream ss;
		ss << assert_in.rdbuf();
		assert_content = ss.str();
	}

	std::ostringstream report;
	report << "## Crash Report\n\n";
	if (!assert_content.empty()) {
		report << "### Failed Assertion\n```\n" << assert_content << "```\n\n";
	}
	if (!info_content.empty()) {
		report << "### Info\n```\n" << info_content << "```\n\n";
	}

	report << "### Backtrace\n\n";
	report << "| Frame | Address | Function | Location |\n";
	report << "|---|---|---|---|\n";

	std::ifstream stack_in(stack_path, std::ios::binary);
	if (stack_in) {
		uint64_t ip_addr;
		std::vector<uintptr_t> raw_ips;
		while (stack_in.read(reinterpret_cast<char *>(&ip_addr), sizeof(ip_addr))) {
			raw_ips.push_back(static_cast<uintptr_t>(ip_addr));
		}
		stack_in.close();

		auto resolved = turbostar::address_lookup::resolve_addresses(raw_ips, maps_path.native());
		int frame = 0;
		static std::string project_root = project_manager::get_instance().get_project_root();
		std::string prefix = project_root;
		if (!prefix.empty() && prefix.back() != '/') {
			prefix += "/";
		}

		struct codemap_report_row {
			std::string rel_path;
			std::string symbol_name;
			int start_line;
			int end_line;
			int line_count;
			int frame_idx;
			int frame_line;
		};

		std::vector<codemap_report_row> codemap_rows;
		std::set<std::tuple<std::string, std::string, int>> seen_symbols;

		for (size_t i = 0; i < raw_ips.size(); ++i) {
			const auto &res = resolved[i];
			if (res.function_name == "__libc_start_main" || res.function_name == "__libc_start_call_main" ||
			    res.function_name == "__libc_start_main_impl" || res.function_name == "_start") {
				break;
			}

			int frame_idx = frame++;
			std::string location = res.location;
			size_t colon_pos = location.find_last_of(':');
			if (colon_pos != std::string::npos && location.length() > 0 && location[0] != '?') {
				size_t first_colon = location.find(':');
				std::string raw_file = location.substr(0, first_colon);
				std::string line_part = location.substr(first_colon);

				fs::path p(raw_file);
				if (!p.is_absolute()) {
					p = fs::path(project_root) / p;
				}
				p = p.lexically_normal();
				std::string full_file_path = p.string();

				bool is_project = false;
				std::string rel_file_path = fs_utils::make_relative_to_project(full_file_path);
				if (full_file_path.starts_with(prefix)) {
					is_project = true;
				}

				location = rel_file_path + line_part;


				// Extract line number for codemap symbol lookup
				if (is_project && fs::exists(full_file_path)) {
					size_t num_start = (line_part.length() > 1 && line_part[0] == ':') ? 1 : 0;
					size_t num_end = line_part.find(':', num_start);
					std::string line_num_str = (num_end != std::string::npos) ?
						line_part.substr(num_start, num_end - num_start) : line_part.substr(num_start);
					int line_num = 0;
					try {
						line_num = std::stoi(line_num_str);
					} catch (...) {
						line_num = 0;
					}

					if (line_num > 0) {
						auto symbols = tools::get_document_codemap_symbols(full_file_path, /*min_lines=*/1);
						const tools::codemap_symbol_info *enc_sym = tools::find_enclosing_symbol(symbols, line_num);
						if (enc_sym) {
							auto sym_key = std::make_tuple(rel_file_path, enc_sym->name, enc_sym->start_line);
							if (!seen_symbols.contains(sym_key)) {
								seen_symbols.insert(sym_key);
								int count = std::max(1, enc_sym->end_line - enc_sym->start_line + 1);
								codemap_rows.push_back({rel_file_path, enc_sym->name, enc_sym->start_line, enc_sym->end_line, count, frame_idx, line_num});
							}
						}
					}
				}
			} else if (location.starts_with(prefix)) {
				location = location.substr(prefix.length());
			}

			report << std::format("| {} | `0x{:x}` | `{}` | {} |\n", frame_idx, raw_ips[i], res.function_name, location);
		}

		if (!codemap_rows.empty()) {
			report << "\n### Codemap Summary\n\n";
			report << "| File | Symbol | Start Line | End Line | Lines | Frame Note |\n";
			report << "|---|---|---|---|---|---|\n";
			for (const auto &row : codemap_rows) {
				report << std::format("| `{}` | `{}` | {} | {} | {} | Frame {} (Line {}) |\n",
					row.rel_path, row.symbol_name, row.start_line, row.end_line, row.line_count, row.frame_idx, row.frame_line);
			}
		}
	}

	fs::path tmp_report_path = report_path;
	tmp_report_path.replace_extension(".tmp");
	std::ofstream out(tmp_report_path);
	if (out) {
		out << report.str();
		out.close();
		std::error_code ec;
		fs::rename(tmp_report_path, report_path, ec);
	}
}

std::string crashdump_manager::refresh(std::string_view /*project_hash*/)
{
	std::lock_guard<std::mutex> lock(mutex_);

	std::string dump_dir = fs_utils::get_project_dump_dir();
	if (!fs::exists(dump_dir))
		return "";

	std::string new_dumps_report;
	bool found_new = false;

	for (const auto &entry : fs::directory_iterator(dump_dir)) {
		if (!entry.is_directory())
			continue;

		std::string dir_name = entry.path().filename().string();
		if (!dir_name.starts_with("crash_"))
			continue;

		std::string crash_id = dir_name.substr(6);

		if (seen_crash_ids_.contains(crash_id))
			continue;

		generate_report_if_needed(entry.path().string());

		crashdump_info info;
		info.crash_id = crash_id;

		// Extract signal and crash cookie from info.txt if it exists
		fs::path info_path = entry.path() / "info.txt";
		std::ifstream info_in(info_path);
		std::string sig_str = "Unknown";
		std::string cookie_str = "";
		if (info_in) {
			std::string line;
			constexpr std::string_view sig_prefix = "Signal: ";
			constexpr std::string_view cookie_prefix = "CrashCookie: ";
			while (std::getline(info_in, line)) {
				if (line.starts_with(sig_prefix)) {
					sig_str = line.substr(sig_prefix.length());
				} else if (line.starts_with(cookie_prefix)) {
					cookie_str = line.substr(cookie_prefix.length());
				}
			}
		}
		info.signal = sig_str;
		info.crash_cookie = cookie_str;

		// Extract executable name from maps
		std::string exe_name = "App";
		fs::path maps_path = entry.path() / "maps.txt";
		if (fs::exists(maps_path)) {
			auto maps = parse_maps(maps_path.string());
			exe_name = extract_executable_name(maps);
		}
		info.executable = exe_name;

		// Extract timestamp from directory modification time
		std::string timestamp_str = "Recent";
		std::error_code ec;
		auto mtime = fs::last_write_time(entry.path(), ec);
		if (!ec) {
			auto sctime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			    mtime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
			std::time_t timet = std::chrono::system_clock::to_time_t(sctime);
			std::tm *local_tm = std::localtime(&timet);
			if (local_tm) {
				char time_buf[64];
				if (std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", local_tm) > 0) {
					timestamp_str = time_buf;
				}
			}
		}
		info.timestamp = timestamp_str;

		// Grab the generated markdown
		fs::path report_path = entry.path() / "report.md";
		std::ifstream report_in(report_path);
		if (report_in) {
			std::ostringstream ss;
			ss << report_in.rdbuf();
			info.raw_info = ss.str();
		}

		crashdumps_.push_back(info);
		seen_crash_ids_.insert(crash_id);

		if (!found_new) {
			new_dumps_report =
			    "### New Crashdumps Detected\n| Crash ID | Timestamp | Executable | Signal | Cookie |\n|---|---|---|---|---|\n";
			found_new = true;
		}
		new_dumps_report += info.to_markdown_row() + "\n";
	}

	return new_dumps_report;
}

const std::vector<crashdump_info> &crashdump_manager::get_crashdumps() const
{
	return crashdumps_;
}

std::vector<crashdump_info> crashdump_manager::get_crashdumps_for_cookie(std::string_view cookie) const
{
	std::vector<crashdump_info> res;
	for (const auto &c : crashdumps_) {
		if (c.crash_cookie == cookie || (!cookie.empty() && c.crash_cookie.starts_with(cookie))) {
			res.push_back(c);
		}
	}
	return res;
}

std::vector<crashdump_info> crashdump_manager::get_crashdumps_for_run(int run_id) const
{
	std::string cookie_prefix = "run_" + std::to_string(run_id);
	return get_crashdumps_for_cookie(cookie_prefix);
}

std::string crashdump_manager::get_markdown_table(size_t limit) const
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (crashdumps_.empty()) {
		return "No crash dumps found.";
	}

	std::string table;
	size_t start_idx = 0;
	if (limit > 0 && crashdumps_.size() > limit) {
		start_idx = crashdumps_.size() - limit;
		table += std::format("*(Showing the {} most recent crash dumps out of {} total)*\n\n", limit, crashdumps_.size());
	}

	table += "| Crash ID | Timestamp | Executable | Signal | Cookie |\n|---|---|---|---|---|\n";
	for (size_t i = start_idx; i < crashdumps_.size(); ++i) {
		table += crashdumps_[i].to_markdown_row() + "\n";
	}
	return table;
}

void crashdump_manager::clear_all()
{
	std::lock_guard<std::mutex> lock(mutex_);

	crashdumps_.clear();
	seen_crash_ids_.clear();

	std::string dump_dir = fs_utils::get_project_dump_dir();
	if (fs::exists(dump_dir)) {
		std::error_code ec;
		fs::remove_all(dump_dir, ec);
		fs::create_directories(dump_dir, ec);
	}
}

std::string crashdump_manager::format_crash_notification(std::span<const crashdump_info> dumps)
{
	if (dumps.empty())
		return "";

	if (dumps.size() == 1) {
		return std::format(
		    "\n\nCRASH DETECTED: Application crashed (Crash ID: {}). Please use 'crashdump_get_info' with crash_id '{}' to investigate stack trace and details.",
		    dumps[0].crash_id, dumps[0].crash_id);
	}

	std::string ids;
	for (size_t i = 0; i < dumps.size(); ++i) {
		if (i > 0)
			ids += ", ";
		ids += dumps[i].crash_id;
	}
	return std::format(
	    "\n\nCRASH DETECTED: {} crash(es) occurred during execution (Crash IDs: {}). Please use 'crashdump_list' and 'crashdump_get_info' to investigate.",
	    dumps.size(), ids);
}

std::string crashdump_manager::format_crash_notification(size_t crash_count)
{
	if (crash_count == 0)
		return "";
	if (crash_count == 1) {
		return "\n\nCRASH DETECTED: 1 new crash occurred during execution. Please use 'crashdump_list' and 'crashdump_get_info' to investigate.";
	}
	return std::format(
	    "\n\nCRASH DETECTED: {} new crash(es) occurred during execution. Please use 'crashdump_list' and 'crashdump_get_info' to investigate.",
	    crash_count);
}