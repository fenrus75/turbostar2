#include "fs_glob.h"
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <re2/re2.h>
#include "../../fs_utils.h"
#include "../../config_manager.h"

namespace fs = std::filesystem;

namespace tools {

static std::string glob_to_regex(const std::string& pattern) {
    std::string regex;
    regex.reserve(pattern.size() * 2);
    size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '*') {
            if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
                // "**"
                if (i + 2 < pattern.size() && pattern[i + 2] == '/') {
                    regex += "(?:[^/]+/)*";
                    i += 3;
                } else {
                    regex += ".*";
                    i += 2;
                }
            } else {
                // "*"
                regex += "[^/]*";
                i++;
            }
        } else if (pattern[i] == '?') {
            regex += "[^/]";
            i++;
        } else if (pattern[i] == '.' || pattern[i] == '+' || pattern[i] == '^' || 
                   pattern[i] == '$' || pattern[i] == '(' || pattern[i] == ')' || 
                   pattern[i] == '[' || pattern[i] == ']' || pattern[i] == '{' || 
                   pattern[i] == '}' || pattern[i] == '|' || pattern[i] == '\\') {
            regex += '\\';
            regex += pattern[i];
            i++;
        } else {
            regex += pattern[i];
            i++;
        }
    }
    return "^" + regex + "$";
}

fs_glob_tool::fs_glob_tool(std::string pattern)
    : llm_tool_action("Globbing for " + pattern), pattern_(std::move(pattern)) {}

bool fs_glob_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_glob_tool::execute(agentlib::tool_context& ctx) {
    fs::path root_path = ctx.fs_security.get_working_directory();
    std::string build_dir = config_manager::get_instance().get_build_directory();

    // Normalize pattern
    std::string norm_pattern = pattern_;
    std::replace(norm_pattern.begin(), norm_pattern.end(), '\\', '/');
    if (norm_pattern.starts_with("./")) {
        norm_pattern = norm_pattern.substr(2);
    } else if (norm_pattern.starts_with("/")) {
        norm_pattern = norm_pattern.substr(1);
    }

    std::string regex_str = glob_to_regex(norm_pattern);
    re2::RE2::Options options;
    options.set_case_sensitive(true);
    re2::RE2 regex(regex_str, options);

    if (!regex.ok()) {
        set_failure(ctx, "Invalid glob pattern translation: " + regex.error());
        return "Error: Invalid glob pattern. Failed to translate pattern to regex.";
    }

    std::vector<std::string> matches;
    size_t total_matches = 0;
    const size_t max_results = 100;

    try {
        for (auto it = fs::recursive_directory_iterator(root_path, fs::directory_options::skip_permission_denied);
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

            std::string rel_path_str = fs::relative(path, root_path).string();
            std::replace(rel_path_str.begin(), rel_path_str.end(), '\\', '/');

            if (re2::RE2::FullMatch(rel_path_str, regex)) {
                // Check read access permissions
                std::string safe_resolved_path;
                std::string out_error;
                if (ctx.fs_security.validate_access(rel_path_str, agentlib::access_type::read, safe_resolved_path, out_error)) {
                    matches.push_back(rel_path_str);
                }
            }
        }
    } catch (const std::exception& e) {
        set_failure(ctx, std::string("Search traversal error: ") + e.what());
        return "Error traversing directory: " + std::string(e.what());
    }

    total_matches = matches.size();

    // Sort matching paths alphabetically
    std::sort(matches.begin(), matches.end());

    // Truncate to max_results after sorting
    if (matches.size() > max_results) {
        matches.resize(max_results);
    }

    std::stringstream ss;
    if (matches.empty()) {
        set_success(ctx, "No matches found");
        return "No matches found for glob pattern '" + pattern_ + "'.";
    }

    ss << "# Glob Results for '" << pattern_ << "' (" << total_matches << " matches):\n\n";
    for (const auto& rel_path : matches) {
        fs::path abs_path = root_path / rel_path;
        std::string info = "";
        if (fs::is_regular_file(abs_path)) {
            try {
                auto size_bytes = fs::file_size(abs_path);
                std::string size_lines = fs_utils::count_lines_in_file(abs_path.string());
                info = " (" + std::to_string(size_bytes) + " bytes, " + size_lines + " lines)";
            } catch (...) {
                // Ignore failures to read metadata (like permissions/symlinks)
            }
        }
        ss << "- `" << rel_path << "`" << info << "\n";
    }

    if (total_matches > max_results) {
        ss << "\n*Note: Limit of " << max_results << " results reached (showing first " << max_results << " of " << total_matches << " matches). Consider narrowing your pattern.*\n";
    }

    set_success(ctx, "Found " + std::to_string(total_matches) + " matches");
    return ss.str();
}

} // namespace tools
