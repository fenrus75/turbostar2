#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "document.h"

namespace turbostar
{

struct perf_line_sample {
	std::string file_path;
	int line_number{0};
	std::string function_name;
	uint64_t count{0};
	double percentage{0.0};
};

struct perf_function_sample {
	std::string function_name;
	std::string file_path;
	int line_number{0};
	uint64_t count{0};
	double percentage{0.0};
};

struct perf_profile_report {
	uint64_t total_samples{0};
	std::vector<perf_function_sample> top_functions;
	std::vector<perf_line_sample> top_lines;
	std::unordered_map<std::string, std::vector<perf_line_sample>> line_samples_by_file;
};

struct file_perf_state {
	int edit_count{0};
	bool is_valid{true};
};

/*
 * perf_manager coordinates host-side post-processing of raw performance profiling samples
 * produced by libturbocatch.so. It merges raw sample counts, invokes address_lookup for
 * symbol resolution, calculates cycle percentages, and maintains the active profile report.
 */
class perf_manager : public document_listener
{
      public:
	static perf_manager &get_instance();

	// Parse raw sample .dat file and maps .txt file from perf_dir and resolve symbols using address_lookup.
	perf_profile_report parse_and_resolve(const std::string &perf_dir, int target_pid = 0,
					      bool cleanup_raw_files = true);

	// Get active profile report
	perf_profile_report get_active_profile() const;

	// Set active profile report
	void set_active_profile(const perf_profile_report &report);

	// Clear active profile report
	void clear_active_profile();

	// Editor UI Query Interface: Returns percentage (0.0 to 100.0) of global samples for line_number in filename.
	// Returns 0.0 if line has no samples or if file profile is invalid.
	double get_line_profile_percentage(const std::string &filename, int line_number) const;

	// Editor UI Query Interface: Returns status bar message for line_number in filename.
	// Returns "" if line has no samples or if file profile is invalid.
	std::string get_line_profile_statusmsg(const std::string &filename, int line_number) const;

	// Check if profile data for a file is valid (less than modification limit)
	bool is_file_profile_valid(const std::string &filename) const;

	// Manually invalidate profile data for a file (e.g. after too many edits)
	void invalidate_file(const std::string &filename);

	// document_listener overrides
	void on_line_inserted(const std::string &filename, int y) override;
	void on_line_deleted(const std::string &filename, int y) override;

      private:
	perf_manager() = default;
	~perf_manager() = default;
	perf_manager(const perf_manager &) = delete;
	perf_manager &operator=(const perf_manager &) = delete;

	/*
	 * mutex_ protects active_report_ and file_states_ across UI redraw threads, document edit listeners,
	 * background tasks, and agent tool execution.
	 * Locking guidelines: standalone lock; never acquired while holding ncurses or event queue mutexes.
	 */
	mutable std::mutex mutex_;
	perf_profile_report active_report_;
	std::unordered_map<std::string, file_perf_state> file_states_;
};

} // namespace turbostar
