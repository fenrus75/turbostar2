#include "crashdump_manager.h"
#include "fs_utils.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

std::string crashdump_info::to_markdown_row() const {
    std::ostringstream oss;
    oss << "| " << crash_id << " | " << timestamp << " | `" << executable << "` | " << signal << " |";
    return oss.str();
}

crashdump_manager& crashdump_manager::get_instance() {
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

static std::vector<memory_map> parse_maps(const std::string& maps_file) {
    std::vector<memory_map> maps;
    std::ifstream in(maps_file);
    std::string line;
    while (std::getline(in, line)) {
        memory_map m;
        char perms[5];
        char path[1024] = {0};
        unsigned int dev_major, dev_minor;
        int inode;
        
        // Example: 555555554000-555555555000 r-xp 00000000 08:01 123456 /path
        if (sscanf(line.c_str(), "%lx-%lx %4s %lx %x:%x %d %1023[^\n]",
                   &m.start, &m.end, perms, &m.offset,
                   &dev_major, &dev_minor, &inode, path) >= 7) {
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

void crashdump_manager::generate_report_if_needed(const std::string& crash_dir) {
    fs::path report_path = fs::path(crash_dir) / "report.md";
    if (fs::exists(report_path)) return;

    fs::path maps_path = fs::path(crash_dir) / "maps.txt";
    fs::path stack_path = fs::path(crash_dir) / "stack.bin";
    fs::path info_path = fs::path(crash_dir) / "info.txt";

    if (!fs::exists(maps_path) || !fs::exists(stack_path)) return;

    auto maps = parse_maps(maps_path.string());
    
    std::string info_content;
    std::ifstream info_in(info_path);
    if (info_in) {
        std::ostringstream ss;
        ss << info_in.rdbuf();
        info_content = ss.str();
    }

    std::ostringstream report;
    report << "## Crash Report\n\n";
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
        while (stack_in.read(reinterpret_cast<char*>(&ip), sizeof(ip))) {
            bool found = false;
            for (const auto& m : maps) {
                if (ip >= m.start && ip < m.end) {
                    uint64_t rel_addr = ip - m.start + m.offset;
                    std::string func_name = "??";
                    std::string location = "??";
                    
                    if (!m.path.empty() && m.path[0] == '/') {
                        std::ostringstream cmd;
                        cmd << "addr2line -C -f -e '" << m.path << "' " << std::hex << rel_addr;
                        
                        std::string output;
                        FILE* pipe = popen(cmd.str().c_str(), "r");
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

                    report << "| " << std::dec << frame << " | `0x" << std::hex << ip << "` | `" << func_name << "` | " << location << " |\n";
                    found = true;
                    break;
                }
            }
            if (!found) {
                report << "| " << std::dec << frame << " | `0x" << std::hex << ip << "` | `??` | [unmapped] |\n";
            }
            frame++;
        }
    }

    std::ofstream out(report_path);
    if (out) {
        out << report.str();
    }
}

std::string crashdump_manager::refresh(const std::string& /*project_hash*/) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string dump_dir = fs_utils::get_project_dump_dir();
    if (!fs::exists(dump_dir)) return "";

    std::string new_dumps_report;
    bool found_new = false;

    for (const auto& entry : fs::directory_iterator(dump_dir)) {
        if (!entry.is_directory()) continue;
        
        std::string dir_name = entry.path().filename().string();
        if (!dir_name.starts_with("crash_")) continue;
        
        std::string crash_id = dir_name.substr(6);
        
        if (seen_crash_ids_.contains(crash_id)) continue;

        generate_report_if_needed(entry.path().string());
        
        crashdump_info info;
        info.crash_id = crash_id;
        
        // Extract signal from info.txt if it exists
        fs::path info_path = entry.path() / "info.txt";
        std::ifstream info_in(info_path);
        std::string sig_str = "Unknown";
        if (info_in) {
            std::string line;
            if (std::getline(info_in, line) && line.starts_with("Signal: ")) {
                sig_str = line.substr(8);
            }
        }
        info.signal = sig_str;
        info.executable = "App"; // Could be extracted from maps.txt
        info.timestamp = "Recent"; // Could use file mtime
        
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
            new_dumps_report = "### New Crashdumps Detected\n| Crash ID | Timestamp | Executable | Signal |\n|---|---|---|---|\n";
            found_new = true;
        }
        new_dumps_report += info.to_markdown_row() + "\n";
    }

    return new_dumps_report;
}

const std::vector<crashdump_info>& crashdump_manager::get_crashdumps() const {
    return crashdumps_;
}

std::string crashdump_manager::get_markdown_table() const {
    if (crashdumps_.empty()) {
        return "No crash dumps found.";
    }

    std::ostringstream oss;
    oss << "| Crash ID | Timestamp | Executable | Signal |\n";
    oss << "|---|---|---|---|\n";
    for (const auto& dump : crashdumps_) {
        oss << dump.to_markdown_row() << "\n";
    }
    return oss.str();
}
