#include "perf_manager.h"
#include "address_lookup.h"
#include "event_logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
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
			} else if (name.starts_with("perf_maps_") && name.ends_with(".txt")) {
				maps_path = entry.path();
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

	struct func_acc {
		std::string name;
		std::string file_path;
		int line_number{0};
		uint64_t count{0};
	};

	struct line_acc {
		std::string file_path;
		int line_number{0};
		uint64_t count{0};
	};

	std::unordered_map<std::string, func_acc> func_map;
	std::unordered_map<std::string, line_acc> line_map;

	for (size_t i = 0; i < unique_ips.size() && i < resolved_addrs.size(); ++i) {
		uintptr_t ip = unique_ips[i];
		uint64_t count = ip_counts[ip];
		const auto &res = resolved_addrs[i];

		if (!res.function_name.empty() && res.function_name != "??") {
			auto &f = func_map[res.function_name];
			f.name = res.function_name;
			f.count += count;
			if (f.file_path.empty()) {
				f.file_path = res.file_path;
				f.line_number = res.line_number;
			}
		}

		if (!res.file_path.empty() && res.file_path != "??" && res.line_number > 0) {
			std::string line_key = std::format("{}:{}", res.file_path, res.line_number);
			auto &l = line_map[line_key];
			l.file_path = res.file_path;
			l.line_number = res.line_number;
			l.count += count;
		}
	}

	for (const auto &pair : func_map) {
		const auto &f = pair.second;
		double pct = (static_cast<double>(f.count) * 100.0) / static_cast<double>(total_samples);
		report.top_functions.push_back(
			perf_function_sample{.function_name = f.name,
						 .file_path = f.file_path,
						 .line_number = f.line_number,
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
		perf_line_sample ls{.file_path = l.file_path,
				    .line_number = l.line_number,
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
}

} // namespace turbostar
