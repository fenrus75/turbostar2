#pragma once

#include <span>
#include <string>
#include <string_view>
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
    std::string summary; // 1-line crash summary (e.g. "assertion fail at foo.c:23 'c != NULL' in bar()"), empty if uninformative

    std::string to_markdown_row() const;
};

struct crash_frame_info {
    int frame_number{0};
    std::string function_name;
    std::string location;
    std::string suggested_var;
};

class crashdump_manager {
public:
    static crashdump_manager& get_instance();

    // Parses crash dump report to extract the primary crash frame index, function name, and candidate variable name
    crash_frame_info get_crash_frame_info(std::string_view crash_id) const;

    // Cleans GDB function signatures by stripping <optimized out> arguments and excess whitespace
    static std::string clean_function_signature(std::string_view raw_func);

    // Classifies a source location into project-relative path, <libc>, <turbocatch>, or <external>
    static std::string classify_location(std::string_view raw_loc, std::string_view project_root, std::string_view build_dir);


    // Returns a markdown formatted string of newly discovered crashdumps, or empty string if none.
    std::string refresh(std::string_view project_hash = "");
    std::vector<crashdump_info> get_crashdumps() const;
    std::vector<crashdump_info> get_crashdumps_for_cookie(std::string_view cookie) const;
    std::vector<crashdump_info> get_crashdumps_for_run(int run_id) const;
    std::string get_markdown_table(size_t limit = 20) const;
    
    // Deletes all crash dumps from the disk and clears internal state
    void clear_all();

    // Copies a binary into the crash dump folder (executable.bin) for coredump/address resolution
    bool preserve_binary(std::string_view crash_id, const std::string &bin_path);

    // Formats a consistent crash notification message for LLM tools
    static std::string format_crash_notification(std::span<const crashdump_info> dumps);

    static std::string format_crash_notification(size_t crash_count);


private:
    crashdump_manager() = default;
    
    // Internal helper to parse raw dump files and generate report.md
    void generate_report_if_needed(std::string_view crash_dir) const;

    std::vector<crashdump_info> crashdumps_;
    std::set<std::string> seen_crash_ids_;
    /*
     * mutex_ protects the crashdumps_ list and seen_crash_ids_ set.
     * Locking Rules:
     * - Held briefly when refreshing crash dumps, querying list, formatting tables,
     *   or clearing dumps.
     */
    mutable std::mutex mutex_;
};
