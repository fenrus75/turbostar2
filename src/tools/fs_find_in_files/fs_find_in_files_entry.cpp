#include "fs_find_in_files.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../../config_manager.h"

namespace fs = std::filesystem;

namespace tools {

fs_find_in_files_tool::fs_find_in_files_tool(fs_find_in_files_args args) : args_(std::move(args)) {
    RE2::Options options;
    compiled_regex_ = std::make_unique<RE2>(args_.pattern, options);
}

std::string fs_find_in_files_tool::escape_markdown(const std::string& text) const {
    std::string escaped;
    for (char c : text) {
        if (c == '`' || c == '*' || c == '_' || c == '[' || c == ']') {
            escaped += '\\';
        }
        escaped += c;
    }
    return escaped;
}

bool fs_find_in_files_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true; // Directory path is validated in the security stage
}

std::string fs_find_in_files_tool::execute(agentlib::tool_context& ctx) {
    if (!compiled_regex_->ok()) {
        return "Error: Invalid regular expression pattern. " + compiled_regex_->error();
    }

    std::string build_dir = config_manager::get_instance().get_build_directory();
    fs::path root_path = ctx.fs_security.get_working_directory();
    
    // Use the resolved, safe starting path
    fs::path search_path(args_.safe_dir_path);

    std::set<std::string> open_files;
    if (ctx.doc_provider) {
        auto paths = ctx.doc_provider->get_open_document_paths();
        for (const auto& p : paths) {
            open_files.insert(p);
        }
    }

    int total_detailed_matches = 0;
    std::map<std::string, std::vector<std::pair<int, std::string>>> detailed_matches;
    std::set<std::string> overflow_files;

    try {
        for (auto it = fs::recursive_directory_iterator(search_path, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            
            const auto& path = it->path();

            if (it->is_directory()) {
                std::string name = path.filename().string();
                bool is_top_level = !path.parent_path().has_relative_path() || path.parent_path() == root_path;

                // Skip hidden dirs, build dirs, and tmp/temp
                if (name.front() == '.' || name == build_dir || name == "tmp" || name == "temp" ||
                    (is_top_level && name.starts_with("build"))) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            if (!fs::is_regular_file(path)) {
                continue;
            }

            if (args_.include_ext) {
                if (path.extension().string() != *args_.include_ext) {
                    continue;
                }
            }

            std::string abs_path_str = path.string();
            std::string rel_path_str = fs::relative(path, root_path).string();

            // 1. Check if the file is an open editor buffer
            if (open_files.contains(abs_path_str) && ctx.doc_provider) {
                auto snapshot = ctx.doc_provider->get_open_document(abs_path_str);
                if (snapshot) {
                    for (size_t i = 0; i < snapshot->get_line_count(); ++i) {
                        std::string line_text = snapshot->get_line_text(i);
                        if (RE2::PartialMatch(line_text, *compiled_regex_)) {
                            if (total_detailed_matches < args_.max_results) {
                                detailed_matches[rel_path_str].push_back({static_cast<int>(i + 1), line_text});
                                total_detailed_matches++;
                            } else {
                                overflow_files.insert(rel_path_str);
                                break; // Stop detailed scanning for this file, just record it has a match
                            }
                        }
                    }
                }
            } 
            // 2. Fallback to mmap disk read
            else {
                int fd = open(abs_path_str.c_str(), O_RDONLY);
                if (fd != -1) {
                    struct stat sb;
                    if (fstat(fd, &sb) == 0 && sb.st_size > 0 && sb.st_size < 50 * 1024 * 1024) { // Ignore empty or >50MB files
                        void* addr = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                        if (addr != MAP_FAILED) {
                            const char* data = static_cast<const char*>(addr);
                            size_t size = sb.st_size;
                            
                            // Check if it's likely a binary file (contains null bytes in first chunk)
                            bool is_binary = false;
                            size_t check_len = std::min<size_t>(size, 1024);
                            for (size_t i = 0; i < check_len; ++i) {
                                if (data[i] == '\0') {
                                    is_binary = true;
                                    break;
                                }
                            }

                            if (!is_binary) {
                                size_t pos = 0;
                                int line_number = 1;
                                while (pos < size) {
                                    const char* nl = static_cast<const char*>(memchr(data + pos, '\n', size - pos));
                                    size_t len = nl ? (nl - (data + pos)) : (size - pos);
                                    
                                    re2::StringPiece sp(data + pos, len);
                                    if (RE2::PartialMatch(sp, *compiled_regex_)) {
                                        if (total_detailed_matches < args_.max_results) {
                                            std::string line_str(data + pos, len);
                                            // Handle potential \r
                                            if (!line_str.empty() && line_str.back() == '\r') {
                                                line_str.pop_back();
                                            }
                                            detailed_matches[rel_path_str].push_back({line_number, line_str});
                                            total_detailed_matches++;
                                        } else {
                                            overflow_files.insert(rel_path_str);
                                            break;
                                        }
                                    }
                                    
                                    if (!nl) break;
                                    pos += len + 1;
                                    line_number++;
                                }
                            }
                            munmap(addr, sb.st_size);
                        }
                    }
                    close(fd);
                }
            }
            
            // If we've hit max results, we could keep scanning to populate overflow_files.
            // For massive codebases, finding ALL files might take a while, but it's useful.
            // If performance becomes an issue, we could cap overflow_files at e.g. 100.
            if (overflow_files.size() > 50) {
                 break; // Hard cap on overflow files to prevent infinite hangs
            }
        }
    } catch (const std::exception& e) {
        return "Error during search traversal: " + std::string(e.what());
    }

    if (detailed_matches.empty() && overflow_files.empty()) {
        return "No matches found.";
    }

    std::stringstream ss;
    ss << "Found " << total_detailed_matches;
    if (!overflow_files.empty()) {
         ss << "+";
    }
    ss << " matches across " << (detailed_matches.size() + overflow_files.size()) << " files:\n\n";

    for (const auto& [file, matches] : detailed_matches) {
        ss << "### `" << file << "`\n";
        for (const auto& match : matches) {
            std::string content = match.second;
            // Truncate excessively long lines
            if (content.length() > 200) {
                content = content.substr(0, 197) + "...";
            }
            ss << "* **Line " << match.first << ":** `" << escape_markdown(content) << "`\n";
        }
        ss << "\n";
    }

    if (!overflow_files.empty()) {
        ss << "---\n";
        ss << "*Note: `max_results` (" << args_.max_results << ") limit reached. Additional matches were found in the following files. Consider narrowing your search or specifying a `dir_path`.*\n";
        for (const auto& f : overflow_files) {
            ss << "- `" << f << "`\n";
        }
    }

    return ss.str();
}

} // namespace tools