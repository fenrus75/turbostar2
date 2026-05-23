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
    std::string raw_info;

    std::string to_markdown_row() const;
};

class crashdump_manager {
public:
    static crashdump_manager& get_instance();

    // Returns a markdown formatted string of newly discovered crashdumps, or empty string if none.
    std::string refresh(const std::string& project_hash);
    const std::vector<crashdump_info>& get_crashdumps() const;
    std::string get_markdown_table() const;

private:
    crashdump_manager() = default;
    
    // Internal helper to parse raw dump files and generate report.md
    void generate_report_if_needed(const std::string& crash_dir);

    std::vector<crashdump_info> crashdumps_;
    std::set<std::string> seen_crash_ids_;
    std::mutex mutex_;
};
