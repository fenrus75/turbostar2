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

namespace tools {

fs_list_dir_tool::fs_list_dir_tool(std::string safe_path) : safe_path_(std::move(safe_path)) {}

bool fs_list_dir_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (!std::filesystem::is_directory(safe_path_)) {
        out_error = "Path is not a directory: " + safe_path_;
        return false;
    }
    return true;
}

std::string fs_list_dir_tool::count_lines_if_text(const std::string& filepath) const {
    struct stat sb;
    if (stat(filepath.c_str(), &sb) == -1) {
        return "";
    }

    // Skip excessively large files (e.g., > 20MB) to prevent stalls
    if (sb.st_size > 20 * 1024 * 1024 || sb.st_size == 0) {
        return "";
    }

    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd == -1) {
        return "";
    }

    // Map the file
    void* map = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // Can close immediately after mmap

    if (map == MAP_FAILED) {
        return "";
    }

    const char* data = static_cast<const char*>(map);

    // Heuristic: Check the first 4KB for null bytes to detect binary files
    size_t check_len = std::min<size_t>(sb.st_size, 4096);
    if (memchr(data, '\0', check_len) != nullptr) {
        munmap(map, sb.st_size);
        return ""; // Looks like a binary file
    }

    // Fast line counting using memchr
    size_t lines = 0;
    const char* p = data;
    const char* end = data + sb.st_size;

    while (p < end) {
        const char* next = static_cast<const char*>(memchr(p, '\n', end - p));
        if (next == nullptr) {
            break;
        }
        lines++;
        p = next + 1;
    }

    // If the file doesn't end with a newline but has content, count the last line
    if (sb.st_size > 0 && *(end - 1) != '\n') {
        lines++;
    }

    munmap(map, sb.st_size);
    return std::to_string(lines);
}

std::string fs_list_dir_tool::execute(agentlib::tool_context& ctx) {
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
                size_lines = count_lines_if_text(resolved_path);
            }

            auto p = entry.status().permissions();
            perms += (p & std::filesystem::perms::owner_read) != std::filesystem::perms::none ? "R" : "-";
            perms += (p & std::filesystem::perms::owner_write) != std::filesystem::perms::none ? "W" : "-";
            perms += (p & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ? "X" : "-";

            ss << "| " << filename << " | " << type << " | " << size_bytes << " | " << size_lines << " | " << perms << " |\n";
        }
    } catch (const std::exception& e) {
        return "Error reading directory: " + std::string(e.what());
    }

    return ss.str();
}

} // namespace tools
