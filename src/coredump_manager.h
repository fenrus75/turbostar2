#pragma once

#include <string>
#include <vector>
#include <set>
#include <mutex>

struct coredump_info {
    int pid;
    std::string timestamp;
    std::string executable;
    std::string signal;
    std::string unit;
    std::string raw_info;

    std::string to_markdown_row() const;
};

class coredump_manager {
public:
    static coredump_manager& get_instance();

    // Returns a markdown formatted string of newly discovered coredumps, or empty string if none.
    std::string refresh(const std::string& project_hash);
    const std::vector<coredump_info>& get_coredumps() const;
    std::string get_markdown_table() const;

private:
    coredump_manager() = default;

    std::vector<coredump_info> coredumps_;
    std::set<int> seen_pids_;
    std::mutex mutex_;
};