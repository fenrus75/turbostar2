#pragma once

#include <string>
#include <vector>
#include <set>
#include <mutex>

struct crashdump_info {
    std::string crash_id; // e.g., "1234" (PID or timestamp)
    std::string timestamp;
    std::string executable;
    std::string signal;
    std::string crash_cookie;
    std::string raw_info;

    std::string to_markdown_row() const;
};

class crashdump_manager {
public:
    static crashdump_manager& get_instance();

    // Returns a markdown formatted string of newly discovered crashdumps, or empty string if none.
    std::string refresh(const std::string& project_hash);
    const std::vector<crashdump_info>& get_crashdumps() const;
    std::vector<crashdump_info> get_crashdumps_for_cookie(const std::string& cookie) const;
    std::vector<crashdump_info> get_crashdumps_for_run(int run_id) const;
    std::string get_markdown_table() const;
    
    // Deletes all crash dumps from the disk and clears internal state
    void clear_all();

    // Formats a consistent crash notification message for LLM tools
    static std::string format_crash_notification(const std::vector<crashdump_info>& dumps);
    static std::string format_crash_notification(size_t crash_count);


private:
    crashdump_manager() = default;
    
    // Internal helper to parse raw dump files and generate report.md
    void generate_report_if_needed(const std::string& crash_dir) const;

    std::vector<crashdump_info> crashdumps_;
    std::set<std::string> seen_crash_ids_;
    /*
     * mutex_ protects the crashdumps_ list and seen_crash_ids_ set.
     * Locking Rules:
     * - Held briefly when refreshing crash dumps, querying list, formatting tables,
     *   or clearing dumps.
     */
    std::mutex mutex_;
};
