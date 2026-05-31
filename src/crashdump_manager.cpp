#include "crashdump_manager.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"

namespace fs = std::filesystem;

std::string crashdump_info::to_markdown_row() const
{
	std::ostringstream oss;
	oss << "| " << crash_id << " | " << timestamp << " | `" << executable << "` | " << signal << " |";
	return oss.str();
}

crashdump_manager &crashdump_manager::get_instance()
{
	static crashdump_manager instance;
	return instance;
}

struct memory_map {
	uint64_t start;
	uint64_t end;
	uint64_t offset;
	std::string perms;
	std::string path;
};

static std::vector<memory_map> parse_maps(const std::string &maps_file)
{
	std::vector<memory_map> maps;
	std::ifstream in(maps_file);
	if (!in) {
		std::cerr << "Warning: failed to open maps file: " << maps_file << std::endl;
		return maps;
	}
	std::string line;
	constexpr int min_scanf_fields = 7;
	while (std::getline(in, line)) {
		memory_map m;
		char perms[5];
		char path[1024] = {0};
		unsigned int dev_major, dev_minor;
		int inode;

		// Example: 555555554000-555555555000 r-xp 00000000 08:01 123456 /path
		if (sscanf(line.c_str(), "%lx-%lx %4s %lx %x:%x %d %1023[^\n]", &m.start, &m.end, perms, &m.offset, &dev_major, &dev_minor,
			   &inode, path) >= min_scanf_fields) {
			m.perms = perms;
			m.path = path;

			// Trim leading spaces from path
			size_t start_pos = m.path.find_first_not_of(" \t");
			if (start_pos != std::string::npos) {
				m.path = m.path.substr(start_pos);
			} else {
				m.path = "";
			}

			maps.push_back(m);
		}
	}
	return maps;
}

static std::string extract_executable_name(const std::vector<memory_map> &maps)
{
	for (const auto &m : maps) {
		if (m.perms.find('x') != std::string::npos && m.offset == 0 && !m.path.empty() && m.path[0] == '/') {
			if (m.path.ends_with(".so") || m.path.find(".so.") != std::string::npos) {
				continue;
			}
			return fs::path(m.path).filename().string();
		}
	}
	return "App";
}

void crashdump_manager::generate_report_if_needed(const std::string &crash_dir) const
{
	fs::path report_path = fs::path(crash_dir) / "report.md";
	if (fs::exists(report_path))
		return;

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
		uint64_t ip;
		int frame = 0;
		while (stack_in.read(reinterpret_cast<char *>(&ip), sizeof(ip))) {
			bool found = false;
			auto it = std::lower_bound(maps.begin(), maps.end(), ip,
						   [](const memory_map &m, uint64_t addr) { return m.end <= addr; });

			if (it != maps.end() && ip >= it->start && ip < it->end) {
				const auto &m = *it;
				uint64_t rel_addr = ip - m.start + m.offset;
				std::string func_name = "??";
				std::string location = "??";

				if (!m.path.empty() && m.path[0] == '/') {
					std::ostringstream cmd;
					cmd << "addr2line -C -f -e " << fs_utils::escape_shell_arg(m.path) << " " << std::hex << rel_addr;
					event_logger::get_instance().log("ip {}  m.start {}   m.offset{}", ip, m.start, m.offset);
					event_logger::get_instance().log("Running addr2line command: {}", cmd.str());

					std::string output;
					FILE *pipe = popen(cmd.str().c_str(), "r");
					if (pipe) {
						char buffer[128];
						while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
							output += buffer;
						}
						pclose(pipe);
					}

					std::istringstream addr_out(output);
					std::string f_line, l_line;
					if (std::getline(addr_out, f_line) && std::getline(addr_out, l_line)) {
						func_name = f_line;
						location = l_line;
					} else {
						location = m.path;
					}
				} else {
					location = m.path.empty() ? "[unknown]" : m.path;
				}

				// Normalize and strip project root from location if present
				static std::string project_root = project_manager::get_instance().get_project_root();
				std::string prefix = project_root;
				if (!prefix.empty() && prefix.back() != '/') {
					prefix += "/";
				}

				size_t colon_pos = location.find_last_of(':');
				if (colon_pos != std::string::npos && location.length() > 0 && location[0] != '?') {
					std::string file_part = location.substr(0, colon_pos);
					std::string line_part = location.substr(colon_pos);

					fs::path p(file_part);
					if (!p.is_absolute()) {
						p = fs::path(project_root) / p;
					}
					file_part = p.lexically_normal().string();

					if (file_part.starts_with(prefix)) {
						file_part = file_part.substr(prefix.length());
					}
					location = file_part + line_part;
				} else if (location.starts_with(prefix)) {
					location = location.substr(prefix.length());
				}

				auto fmt = report.flags();
				report << "| " << std::dec << frame << " | `0x" << std::hex << ip << "` | `" << func_name << "` | "
				       << location << " |\n";
				report.flags(fmt);
				found = true;
			}
			if (!found) {
				auto fmt = report.flags();
				report << "| " << std::dec << frame << " | `0x" << std::hex << ip << "` | `??` | [unmapped] |\n";
				report.flags(fmt);
			}
			frame++;
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

std::string crashdump_manager::refresh(const std::string & /*project_hash*/)
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

		// Extract signal from info.txt if it exists
		fs::path info_path = entry.path() / "info.txt";
		std::ifstream info_in(info_path);
		std::string sig_str = "Unknown";
		if (info_in) {
			std::string line;
			constexpr std::string_view sig_prefix = "Signal: ";
			if (std::getline(info_in, line) && line.starts_with(sig_prefix)) {
				sig_str = line.substr(sig_prefix.length());
			}
		}
		info.signal = sig_str;

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
			    "### New Crashdumps Detected\n| Crash ID | Timestamp | Executable | Signal |\n|---|---|---|---|\n";
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

std::string crashdump_manager::get_markdown_table() const
{
	if (crashdumps_.empty()) {
		return "No crash dumps found.";
	}

	std::ostringstream oss;
	oss << "| Crash ID | Timestamp | Executable | Signal |\n";
	oss << "|---|---|---|---|\n";
	for (const auto &dump : crashdumps_) {
		oss << dump.to_markdown_row() << "\n";
	}
	return oss.str();
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