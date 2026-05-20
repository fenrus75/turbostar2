#include "fs_read_lines.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>

namespace tools {

fs_read_lines_tool::fs_read_lines_tool(fs_read_lines_args args) : args_(std::move(args)) {}

bool fs_read_lines_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_read_lines_tool::execute(agentlib::tool_context& ctx) {
    // 1. Fallback bounds checks (safeguards for LLM hallucinations)
    int start = std::max(1, args_.start_line);
    int end = std::max(start, args_.end_line);

    // Prevent reading massive blocks that blow out context window
    if (end - start > 2000) {
        end = start + 2000;
    }
    
    // Write them back to args_ so helper methods use the bounded values
    args_.start_line = start;
    args_.end_line = end;

    // 2. Intercept VFS paths (skills://)
    if (args_.safe_path.starts_with("skills://")) {
        auto vfs = ctx.fs_security.get_vfs();
        if (vfs) {
            auto view_opt = vfs->read_file(args_.safe_path);
            if (view_opt) {
                std::string_view view = view_opt.value();
                
                // Very basic line slicing from string_view
                std::stringstream ss;
                int current_line = 1;
                size_t start_pos = 0;
                
                while (start_pos < view.length()) {
                    size_t end_pos = view.find('\n', start_pos);
                    std::string_view line = (end_pos == std::string_view::npos) 
                                          ? view.substr(start_pos) 
                                          : view.substr(start_pos, end_pos - start_pos);
                    
                    if (current_line >= args_.start_line && current_line <= args_.end_line) {
                        ss << line << "\n";
                    } else if (current_line > args_.end_line) {
                        break;
                    }
                    
                    start_pos = (end_pos == std::string_view::npos) ? view.length() : end_pos + 1;
                    current_line++;
                }
                
                if (ss.str().empty()) {
                    return "Requested line range is empty or past the end of the file.";
                }
                return ss.str();
            }
        }
        return "Error: Virtual file not found or not mounted.";
    }

    // 3. Try reading from active editor document first
    if (ctx.doc_provider) {
        auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
        if (doc_snapshot) {
            return read_from_document(doc_snapshot.get());
        }
    }

    // 3. Fallback to direct disk access
    return read_from_disk();
}

std::string fs_read_lines_tool::read_from_document(agentlib::document_snapshot* doc) const {
    std::stringstream ss;
    size_t total_lines = doc->get_line_count();

    int start_idx = args_.start_line - 1;
    int end_idx = std::min<int>(args_.end_line - 1, static_cast<int>(total_lines) - 1);

    if (start_idx >= static_cast<int>(total_lines)) {
        return "Requested start line is past the end of the file.";
    }

    for (int i = start_idx; i <= end_idx; ++i) {
        ss << doc->get_line_text(i) << "\n";
    }

    return ss.str();
}

std::string fs_read_lines_tool::read_from_disk() const {
    struct stat sb;
    if (stat(args_.safe_path.c_str(), &sb) == -1) {
        return "Error: File does not exist or cannot be accessed.";
    }

    // Skip excessively large files to prevent RAM exhaustion
    if (sb.st_size > 50 * 1024 * 1024) {
        return "Error: File is too large (>50MB) to read directly.";
    }

    std::ifstream file(args_.safe_path, std::ios::binary);
    if (!file.is_open()) {
        return "Error: Could not open file for reading.";
    }

    // Check for binary data
    char buffer[4096];
    file.read(buffer, sizeof(buffer));
    size_t bytes_read = file.gcount();
    if (memchr(buffer, '\0', bytes_read) != nullptr) {
        return "Error: File appears to be binary. Cannot read text lines.";
    }

    // Reset stream
    file.clear();
    file.seekg(0);

    std::stringstream ss;
    std::string line;
    int current_line = 1;

    // Discard lines until we reach start_line
    while (current_line < args_.start_line && std::getline(file, line)) {
        current_line++;
    }

    // Read and append requested lines
    while (current_line <= args_.end_line && std::getline(file, line)) {
        // Strip trailing \r if Windows format
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        ss << line << "\n";
        current_line++;
    }

    if (ss.str().empty()) {
        return "Requested line range is empty or past the end of the file.";
    }

    return ss.str();
}

} // namespace tools
