#include "perf_manager.h"
#include "address_lookup.h"
#include "event_logger.h"
#include "fs_utils.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <unordered_map>

namespace fs = std::filesystem;

namespace turbostar
{

perf_manager &perf_manager::get_instance()
{
	static perf_manager instance;
	return instance;
}

perf_profile_report perf_manager::parse_and_resolve(const std::string &perf_dir, int target_pid,
						      bool cleanup_raw_files)
{
	perf_profile_report report;
	auto &logger = event_logger::get_instance();
	logger.log(std::format("perf_manager: Beginning parse_and_resolve for perf_dir='{}' (target_pid={})", perf_dir, target_pid));

	if (perf_dir.empty() || !fs::exists(perf_dir)) {
		logger.log(std::format("perf_manager: Directory '{}' is empty or does not exist.", perf_dir));
		return report;
	}

	// Log directory contents and file sizes for diagnostic visibility
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(perf_dir, ec)) {
		std::string fname = entry.path().filename().string();
		uint64_t fsize = entry.is_regular_file() ? entry.file_size() : 0;
		logger.log(std::format("perf_manager: Found entry in perf_dir: '{}' ({} bytes)", fname, fsize));
		if (fname.starts_with("perf_debug_") && fname.ends_with(".txt")) {
			std::ifstream dbg_in(entry.path());
			if (dbg_in.is_open()) {
				std::string dbg_content((std::istreambuf_iterator<char>(dbg_in)), std::istreambuf_iterator<char>());
				logger.log(std::format("perf_manager: Content of '{}': {}", fname, dbg_content));
			}
		}
	}

	fs::path samples_path;
	fs::path maps_path;

	if (target_pid > 0) {
		std::string pid_str = std::to_string(target_pid);
		samples_path = fs::path(perf_dir) / std::format("perf_samples_{}.dat", pid_str);
		maps_path = fs::path(perf_dir) / std::format("perf_maps_{}.txt", pid_str);
	}

	if (samples_path.empty() || !fs::exists(samples_path)) {
		for (const auto &entry : fs::directory_iterator(perf_dir, ec)) {
			std::string name = entry.path().filename().string();
			if (name.starts_with("perf_samples_") && name.ends_with(".dat")) {
				samples_path = entry.path();
			}
		}
	}

	if (!samples_path.empty() && (maps_path.empty() || !fs::exists(maps_path))) {
		std::string sname = samples_path.filename().string();
		if (sname.starts_with("perf_samples_") && sname.ends_with(".dat")) {
			std::string pid_part = sname.substr(13, sname.size() - 17);
			fs::path candidate = fs::path(perf_dir) / std::format("perf_maps_{}.txt", pid_part);
			if (fs::exists(candidate)) {
				maps_path = candidate;
			}
		}
	}

	if (maps_path.empty() || !fs::exists(maps_path)) {
		for (const auto &entry : fs::directory_iterator(perf_dir, ec)) {
			std::string name = entry.path().filename().string();
			if (name.starts_with("perf_maps_") && name.ends_with(".txt")) {
				maps_path = entry.path();
				break;
			}
		}
	}

	logger.log(std::format("perf_manager: Target samples_path='{}' (exists={}), maps_path='{}' (exists={})",
			       samples_path.string(), fs::exists(samples_path), maps_path.string(), fs::exists(maps_path)));

	if (samples_path.empty() || !fs::exists(samples_path)) {
		logger.log("perf_manager: No valid perf_samples_*.dat file found.");
		if (cleanup_raw_files) {
			if (!maps_path.empty() && fs::exists(maps_path)) {
				fs::remove(maps_path, ec);
			}
		}
		return report;
	}

	struct sample_slot_raw {
		unsigned long ip;
		unsigned long count;
	};

	uint64_t samples_file_size = fs::file_size(samples_path, ec);
	logger.log(std::format("perf_manager: Reading samples binary file '{}' ({} bytes)", samples_path.string(), samples_file_size));

	std::ifstream samples_in(samples_path, std::ios::binary);
	if (!samples_in.is_open()) {
		logger.log(std::format("perf_manager: Failed to open samples file '{}'", samples_path.string()));
		return report;
	}

	std::unordered_map<uintptr_t, uint64_t> ip_counts;
	uint64_t total_samples = 0;
	sample_slot_raw slot;

	while (samples_in.read(reinterpret_cast<char *>(&slot), sizeof(slot))) {
		if (slot.ip != 0 && slot.count > 0) {
			ip_counts[static_cast<uintptr_t>(slot.ip)] += slot.count;
			total_samples += slot.count;
		}
	}
	samples_in.close();

	logger.log(std::format("perf_manager: Total raw samples aggregated: {} across {} unique instruction pointers",
			       total_samples, ip_counts.size()));

	if (total_samples == 0 || ip_counts.empty()) {
		logger.log("perf_manager: Total samples count is 0.");
		if (cleanup_raw_files) {
			fs::remove(samples_path, ec);
			if (!maps_path.empty()) {
				fs::remove(maps_path, ec);
			}
		}
		return report;
	}

	report.total_samples = total_samples;

	std::vector<uintptr_t> unique_ips;
	unique_ips.reserve(ip_counts.size());
	for (const auto &pair : ip_counts) {
		unique_ips.push_back(pair.first);
	}

	std::string maps_arg = maps_path.empty() ? "" : maps_path.string();
	auto resolved_addrs = address_lookup::resolve_addresses(unique_ips, maps_arg);

	logger.log(std::format("perf_manager: Resolved {} unique IPs using maps_arg='{}'. Sample resolution head:",
			       resolved_addrs.size(), maps_arg));
	for (size_t k = 0; k < std::min<size_t>(5, resolved_addrs.size()); ++k) {
		logger.log(std::format("  [IP 0x{:x}] func='{}', file='{}', line={}",
				       unique_ips[k], resolved_addrs[k].function_name, resolved_addrs[k].file_path,
				       resolved_addrs[k].line_number));
	}

	struct func_acc {
		std::string name;
		uint64_t count{0};
		std::unordered_map<std::string, uint64_t> file_counts;
		std::unordered_map<std::string, std::map<int, uint64_t>> file_line_counts;
	};

	struct line_acc {
		std::string file_path;
		int line_number{0};
		uint64_t count{0};
		std::unordered_map<std::string, uint64_t> func_counts;
	};

	std::unordered_map<std::string, func_acc> func_map;
	std::unordered_map<std::string, line_acc> line_map;

	for (size_t i = 0; i < unique_ips.size() && i < resolved_addrs.size(); ++i) {
		uintptr_t ip = unique_ips[i];
		uint64_t count = ip_counts[ip];
		const auto &res = resolved_addrs[i];

		std::string norm_file_path = res.file_path;
		if (!norm_file_path.empty() && norm_file_path != "??") {
			size_t colon_idx = norm_file_path.rfind(':');
			if (colon_idx != std::string::npos && colon_idx + 1 < norm_file_path.size()) {
				bool all_digits = true;
				for (size_t k = colon_idx + 1; k < norm_file_path.size(); ++k) {
					if (!std::isdigit(static_cast<unsigned char>(norm_file_path[k]))) {
						all_digits = false;
						break;
					}
				}
				if (all_digits) {
					norm_file_path = norm_file_path.substr(0, colon_idx);
				}
			}
			norm_file_path = fs_utils::make_relative_to_project(norm_file_path);
		}

		if (!res.function_name.empty() && res.function_name != "??") {
			auto &f = func_map[res.function_name];
			f.name = res.function_name;
			f.count += count;
			if (!norm_file_path.empty() && norm_file_path != "??") {
				f.file_counts[norm_file_path] += count;
				if (res.line_number > 0) {
					f.file_line_counts[norm_file_path][res.line_number] += count;
				}
			}
		}

		if (!norm_file_path.empty() && norm_file_path != "??" && res.line_number > 0) {
			std::string line_key = std::format("{}:{}", norm_file_path, res.line_number);
			auto &l = line_map[line_key];
			l.file_path = norm_file_path;
			l.line_number = res.line_number;
			l.count += count;
			if (!res.function_name.empty() && res.function_name != "??") {
				l.func_counts[res.function_name] += count;
			}
		}
	}

	for (const auto &pair : func_map) {
		const auto &f = pair.second;
		double pct = (static_cast<double>(f.count) * 100.0) / static_cast<double>(total_samples);

		std::string top_file;
		uint64_t max_file_cnt = 0;
		for (const auto &file_pair : f.file_counts) {
			if (file_pair.second > max_file_cnt) {
				max_file_cnt = file_pair.second;
				top_file = file_pair.first;
			}
		}

		int top_line = 0;
		uint64_t max_line_cnt = 0;
		if (!top_file.empty()) {
			auto it = f.file_line_counts.find(top_file);
			if (it != f.file_line_counts.end()) {
				for (const auto &line_pair : it->second) {
					if (line_pair.second > max_line_cnt) {
						max_line_cnt = line_pair.second;
						top_line = line_pair.first;
					}
				}
			}
		}

		report.top_functions.push_back(
			perf_function_sample{.function_name = f.name,
						 .file_path = top_file,
						 .line_number = top_line,
						 .count = f.count,
						 .percentage = pct});
	}

	std::sort(report.top_functions.begin(), report.top_functions.end(),
		  [](const perf_function_sample &a, const perf_function_sample &b) {
			  return a.percentage > b.percentage;
		  });

	for (const auto &pair : line_map) {
		const auto &l = pair.second;
		double pct = (static_cast<double>(l.count) * 100.0) / static_cast<double>(total_samples);

		std::string top_func;
		uint64_t max_func_cnt = 0;
		for (const auto &func_pair : l.func_counts) {
			if (func_pair.second > max_func_cnt) {
				max_func_cnt = func_pair.second;
				top_func = func_pair.first;
			}
		}

		perf_line_sample ls{.file_path = l.file_path,
				    .line_number = l.line_number,
				    .function_name = top_func,
				    .count = l.count,
				    .percentage = pct};
		report.top_lines.push_back(ls);
		report.line_samples_by_file[l.file_path].push_back(ls);
	}

	std::sort(report.top_lines.begin(), report.top_lines.end(),
		  [](const perf_line_sample &a, const perf_line_sample &b) { return a.percentage > b.percentage; });

	for (auto &pair : report.line_samples_by_file) {
		std::sort(pair.second.begin(), pair.second.end(),
			  [](const perf_line_sample &a, const perf_line_sample &b) {
				  return a.line_number < b.line_number;
			  });
	}

	if (cleanup_raw_files) {
		std::error_code ec;
		fs::remove(samples_path, ec);
		if (!maps_path.empty()) {
			fs::remove(maps_path, ec);
		}
	}

	set_active_profile(report);
	logger.log(std::format("perf_manager: Successfully resolved {} top functions and {} top lines across {} total samples.",
			       report.top_functions.size(), report.top_lines.size(), report.total_samples));
	return report;
}

perf_profile_report perf_manager::get_active_profile() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return active_report_;
}

void perf_manager::set_active_profile(const perf_profile_report &report)
{
	std::lock_guard<std::mutex> lock(mutex_);
	active_report_ = report;
}

void perf_manager::clear_active_profile()
{
	std::lock_guard<std::mutex> lock(mutex_);
	active_report_ = perf_profile_report{};
	file_states_.clear();
}

bool perf_manager::is_file_profile_valid(const std::string &filename) const
{
	if (filename.empty()) {
		return false;
	}
	std::string rel_path = fs_utils::make_relative_to_project(filename);
	auto it = file_states_.find(rel_path);
	if (it != file_states_.end()) {
		return it->second.is_valid && it->second.edit_count < 20;
	}
	return true;
}

void perf_manager::invalidate_file(const std::string &filename)
{
	if (filename.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	std::string rel_path = fs_utils::make_relative_to_project(filename);
	file_states_[rel_path].is_valid = false;
}

double perf_manager::get_line_profile_percentage(const std::string &filename, int line_number) const
{
	if (filename.empty() || line_number <= 0) {
		return 0.0;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (active_report_.total_samples == 0) {
		return 0.0;
	}

	std::string rel_path = fs_utils::make_relative_to_project(filename);
	auto state_it = file_states_.find(rel_path);
	if (state_it != file_states_.end() && (!state_it->second.is_valid || state_it->second.edit_count >= 20)) {
		return 0.0;
	}

	auto it = active_report_.line_samples_by_file.find(rel_path);
	if (it == active_report_.line_samples_by_file.end()) {
		it = active_report_.line_samples_by_file.find(filename);
	}
	if (it == active_report_.line_samples_by_file.end()) {
		return 0.0;
	}

	for (const auto &ls : it->second) {
		if (ls.line_number == line_number) {
			return ls.percentage;
		}
	}
	return 0.0;
}

std::string perf_manager::get_line_profile_statusmsg(const std::string &filename, int line_number) const
{
	if (filename.empty() || line_number <= 0) {
		return "";
	}
	std::lock_guard<std::mutex> lock(mutex_);
	if (active_report_.total_samples == 0) {
		return "";
	}

	std::string rel_path = fs_utils::make_relative_to_project(filename);
	auto state_it = file_states_.find(rel_path);
	if (state_it != file_states_.end() && (!state_it->second.is_valid || state_it->second.edit_count >= 20)) {
		return "";
	}

	auto it = active_report_.line_samples_by_file.find(rel_path);
	if (it == active_report_.line_samples_by_file.end()) {
		it = active_report_.line_samples_by_file.find(filename);
	}
	if (it == active_report_.line_samples_by_file.end()) {
		return "";
	}

	for (const auto &ls : it->second) {
		if (ls.line_number == line_number && ls.count > 0) {
			if (!ls.function_name.empty()) {
				return std::format("Perf: {} samples ({:.1f}% global) [{}]", ls.count, ls.percentage, ls.function_name);
			}
			return std::format("Perf: {} samples ({:.1f}% global)", ls.count, ls.percentage);
		}
	}
	return "";
}

void perf_manager::on_line_inserted(const std::string &filename, int y_zero_based)
{
	if (filename.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	std::string rel_path = fs_utils::make_relative_to_project(filename);
	auto &state = file_states_[rel_path];
	state.edit_count++;
	if (state.edit_count >= 20) {
		state.is_valid = false;
	}

	int insert_line = y_zero_based + 1;
	auto it = active_report_.line_samples_by_file.find(rel_path);
	if (it != active_report_.line_samples_by_file.end()) {
		for (auto &ls : it->second) {
			if (ls.line_number >= insert_line) {
				ls.line_number++;
			}
		}
	}
}

void perf_manager::on_line_deleted(const std::string &filename, int y_zero_based)
{
	if (filename.empty()) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex_);
	std::string rel_path = fs_utils::make_relative_to_project(filename);
	auto &state = file_states_[rel_path];
	state.edit_count++;
	if (state.edit_count >= 20) {
		state.is_valid = false;
	}

	int delete_line = y_zero_based + 1;
	auto it = active_report_.line_samples_by_file.find(rel_path);
	if (it != active_report_.line_samples_by_file.end()) {
		auto &samples = it->second;
		for (auto sample_it = samples.begin(); sample_it != samples.end();) {
			if (sample_it->line_number == delete_line) {
				sample_it = samples.erase(sample_it);
			} else {
				if (sample_it->line_number > delete_line) {
					sample_it->line_number--;
				}
				++sample_it;
			}
		}
	}
}

} // namespace turbostar
