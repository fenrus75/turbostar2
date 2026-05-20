#include "fs_list_dir.h"
#include <filesystem>
#include <sstream>
#include <vector>
#include <algorithm>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include "../../fs_utils.h"

namespace tools {

fs_list_dir_tool::fs_list_dir_tool(std::string safe_path) : safe_path_(std::move(safe_path)) {}

bool fs_list_dir_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    if (safe_path_.starts_with("skills://")) {
        auto vfs = ctx.fs_security.get_vfs();
        if (vfs) {
            // Check if there are any mounts with this prefix
            auto listing = vfs->list_directory(safe_path_);
            if (listing.empty() && !vfs->exists(safe_path_)) {
                out_error = "Virtual directory not found or not mounted: " + safe_path_;
                return false;
            }
            return true;
        }
        out_error = "Virtual file system not available.";
        return false;
    }

    if (!std::filesystem::is_directory(safe_path_)) {
        out_error = "Path is not a directory: " + safe_path_;
        return false;
    }
    return true;
}

std::string fs_list_dir_tool::execute(agentlib::tool_context& ctx) {
    if (safe_path_.starts_with("skills://")) {
        auto vfs = ctx.fs_security.get_vfs();
        if (!vfs) return "Error: VFS not available.";

        std::string prefix = safe_path_;
        if (!prefix.ends_with('/')) prefix += '/';

        std::stringstream ss;
        ss << "# Virtual Directory " << prefix << "\n\n";
        ss << "| Filename | File Type | File Size (bytes) | File Size (lines) | Permissions |\n";
        ss << "| -------- | --------- | ----------------- | ----------------- | ----------- |\n";

        auto entries = vfs->list_directory(prefix);
        for (const auto& entry : entries) {
            // Extract filename from URI
            std::string filename = entry.uri.substr(prefix.length());
            // Filter out files in subdirectories
            if (filename.find('/') != std::string::npos) continue;

            ss << "| " << filename << " | " << entry.type << " | " << entry.size << " | " << entry.size_in_lines << " | R-- |\n";
        }
        return ss.str();
    }

    std::filesystem::path relative_path = std::filesystem::relative(safe_path_, ctx.fs_security.get_working_directory());
    std::string rel_str = relative_path.string();
    if (rel_str.empty() || rel_str == ".") {
        rel_str = "/ (Project Root)";
    }

    std::stringstream ss;
    ss << "# Directory " << rel_str << "\n\n";
    ss << "| Filename | File Type | File Size (bytes) | File Size (lines) | Permissions |\n";
    ss << "| -------- | --------- | ----------------- | ----------------- | ----------- |\n";

    try {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(safe_path_)) {
            entries.push_back(entry);
        }

        // Sort alphabetically
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.path().filename().string() < b.path().filename().string();
        });

        for (const auto& entry : entries) {
            std::string path_str = entry.path().string();
            std::string resolved_path;
            std::string error;

            // Strict visibility check: Only list files the LLM is allowed to read
            if (!ctx.fs_security.validate_access(path_str, agentlib::access_type::read, resolved_path, error)) {
                continue;
            }

            std::string filename = entry.path().filename().string();
            std::string type = "Unknown";
            std::string size_bytes = "";
            std::string size_lines = "";
            std::string perms = "";

            if (entry.is_symlink()) {
                type = "L";
            } else if (entry.is_directory()) {
                type = "D";
            } else if (entry.is_regular_file()) {
                type = "F";
                size_bytes = std::to_string(entry.file_size());
                size_lines = fs_utils::count_lines_in_file(resolved_path);
            }

            auto p = entry.status().permissions();
            perms += (p & std::filesystem::perms::owner_read) != std::filesystem::perms::none ? "R" : "-";
            
            // Only report Write if the OS allows it AND the security manager allows it
            bool os_can_write = (p & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
            bool agent_can_write = false;
            if (os_can_write) {
                std::string dump_path;
                std::string dump_err;
                agent_can_write = ctx.fs_security.validate_access(path_str, agentlib::access_type::write, dump_path, dump_err);
            }
            perms += agent_can_write ? "W" : "-";
            
            perms += (p & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ? "X" : "-";

            ss << "| " << filename << " | " << type << " | " << size_bytes << " | " << size_lines << " | " << perms << " |\n";
        }
    } catch (const std::exception& e) {
        return "Error reading directory: " + std::string(e.what());
    }

    return ss.str();
}

} // namespace tools
