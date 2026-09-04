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

fs_glob_tool::fs_glob_tool(fs_glob_args args)
    : llm_tool_action("Globbing for " + args.pattern), args_(std::move(args)) {}

fs_glob_tool::fs_glob_tool(std::string pattern)
    : fs_glob_tool(fs_glob_args{.pattern = std::move(pattern)}) {}

bool fs_glob_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_glob_tool::execute(agentlib::tool_context& ctx) {
    fs::path root_path = ctx.fs_security.get_working_directory();
    std::string build_dir = config_manager::get_instance().get_build_directory();

    const size_t max_results = 100;
    re2::RE2::Options options;
    options.set_case_sensitive(true);

    // Check if pattern or path is a VFS URI (e.g. include://*.h or include://bits/*.h)
    std::string vfs_uri;
    if (args_.pattern.find("://") != std::string::npos) {
        vfs_uri = args_.pattern;
    } else if (args_.path.find("://") != std::string::npos) {
        vfs_uri = args_.path;
        if (!vfs_uri.ends_with("/")) {
            vfs_uri += "/";
        }
        vfs_uri += args_.pattern;
    }

    if (!vfs_uri.empty()) {
        auto vfs = ctx.fs_security.get_vfs();
        if (!vfs) {
            set_failure(ctx, "VFS not available");
            return "Error: VFS not available.";
        }

        size_t scheme_pos = vfs_uri.find("://");
        std::string scheme = vfs_uri.substr(0, scheme_pos + 3);
        std::string path_pattern = vfs_uri.substr(scheme_pos + 3);

        size_t wild_pos = path_pattern.find_first_of("*?");
        std::string dir_prefix;
        if (wild_pos != std::string::npos) {
            size_t slash_pos = path_pattern.find_last_of('/', wild_pos);
            if (slash_pos != std::string::npos) {
                dir_prefix = path_pattern.substr(0, slash_pos);
            }
        } else {
            size_t slash_pos = path_pattern.find_last_of('/');
            if (slash_pos != std::string::npos) {
                dir_prefix = path_pattern.substr(0, slash_pos);
            }
        }

        std::string base_dir_uri = scheme + dir_prefix;
        bool is_recursive = path_pattern.find("**") != std::string::npos;

        std::vector<std::string> candidates;
        std::function<void(const std::string&)> scan_vfs_dir = [&](const std::string &cur_uri) {
            auto listing = vfs->list_directory(cur_uri);
            for (const auto &item : listing) {
                if (item.type == 'F') {
                    candidates.push_back(item.uri);
                } else if (item.type == 'D' && is_recursive) {
                    scan_vfs_dir(item.uri);
                }
            }
        };
        scan_vfs_dir(base_dir_uri);

        std::string regex_str = glob_to_regex(vfs_uri);
        re2::RE2 regex(regex_str, options);

        std::vector<std::string> matches;
        for (const auto &cand : candidates) {
            if (re2::RE2::FullMatch(cand, regex)) {
                matches.push_back(cand);
            }
        }

        std::sort(matches.begin(), matches.end());
        if (matches.size() > max_results) {
            matches.resize(max_results);
        }

        if (matches.empty()) {
            set_success(ctx, "No matches found");
            return fs_utils::wrap_prompt_untrusted_data_tag("fs_glob_result", "No matches found for glob pattern '" + args_.pattern + "'.");
        }

        std::stringstream ss;
        ss << "# Glob Results for '" << args_.pattern << "' (" << matches.size() << " matches):\n\n";
        for (const auto &m : matches) {
            ss << "- `" << m << "`\n";
        }
        set_success(ctx, std::to_string(matches.size()) + " matches");
        return fs_utils::wrap_prompt_untrusted_data_tag("fs_glob_result", ss.str());
    }

    // Normalize pattern
    std::string norm_pattern = args_.pattern;
    std::replace(norm_pattern.begin(), norm_pattern.end(), '\\', '/');
    if (norm_pattern.starts_with("./")) {
        norm_pattern = norm_pattern.substr(2);
    } else if (norm_pattern.starts_with("/")) {
        norm_pattern = norm_pattern.substr(1);
    }

    std::string regex_str = glob_to_regex(norm_pattern);
    re2::RE2 regex(regex_str, options);

    if (!regex.ok()) {
        set_failure(ctx, "Invalid glob pattern translation: " + regex.error());
        return "Error: Invalid glob pattern. Failed to translate pattern to regex.";
    }

    fs::path search_dir = args_.safe_search_path.empty() ? root_path : fs::path(args_.safe_search_path);
    if (!fs::exists(search_dir) || !fs::is_directory(search_dir)) {
        set_failure(ctx, "Directory does not exist: " + args_.path);
        return "Error: Directory does not exist: " + args_.path;
    }

    std::string norm_path = args_.path;
    std::replace(norm_path.begin(), norm_path.end(), '\\', '/');
    while (norm_path.starts_with("./")) {
        norm_path = norm_path.substr(2);
    }

    bool target_is_build = (norm_path == build_dir || norm_path.starts_with(build_dir + "/") ||
                            norm_pattern == build_dir || norm_pattern.starts_with(build_dir + "/") ||
                            norm_path.starts_with("build") || norm_pattern.starts_with("build"));

    bool target_is_tmp = (norm_path == "tmp" || norm_path == "temp" ||
                          norm_path.starts_with("tmp/") || norm_path.starts_with("temp/") ||
                          norm_pattern.starts_with("tmp/") || norm_pattern.starts_with("temp/"));

    std::vector<std::string> matches;
    size_t total_matches = 0;

    try {
        for (auto it = fs::recursive_directory_iterator(search_dir, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            
            const auto& p = it->path();

            if (it->is_directory()) {
                std::string name = p.filename().string();
                bool is_top_level = !p.parent_path().has_relative_path() || p.parent_path() == root_path;

                // Skip hidden dirs, and skip build/tmp unless specifically targeted
                if (name.front() == '.' ||
                    (!target_is_tmp && (name == "tmp" || name == "temp")) ||
                    (!target_is_build && (name == build_dir || (is_top_level && name.starts_with("build"))))) {
                    it.disable_recursion_pending();
                }
                continue;
            }

            std::string rel_to_root = fs::relative(p, root_path).string();
            std::replace(rel_to_root.begin(), rel_to_root.end(), '\\', '/');

            std::string rel_to_search = fs::relative(p, search_dir).string();
            std::replace(rel_to_search.begin(), rel_to_search.end(), '\\', '/');

            std::string filename = p.filename().string();

            bool matched = re2::RE2::FullMatch(rel_to_search, regex) ||
                           re2::RE2::FullMatch(rel_to_root, regex);
            if (!matched && norm_pattern.find('/') == std::string::npos) {
                matched = re2::RE2::FullMatch(filename, regex);
            }

            if (matched) {
                // Check read access permissions
                std::string safe_resolved_path;
                std::string out_error;
                if (ctx.fs_security.validate_access(rel_to_root, agentlib::access_type::read, safe_resolved_path, out_error)) {
                    matches.push_back(rel_to_root);
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
        return fs_utils::wrap_prompt_untrusted_data_tag("fs_glob_result", "No matches found for glob pattern '" + args_.pattern + "'.");
    }

    ss << "# Glob Results for '" << args_.pattern << "' (" << total_matches << " matches):\n\n";
    for (const auto& rel_path : matches) {
        fs::path abs_path = root_path / rel_path;
        std::string info = "";
        if (fs::is_regular_file(abs_path)) {
            try {
                auto size_bytes = fs::file_size(abs_path);
                std::string size_lines;
                if (size_bytes <= 1 * 1024 * 1024) {
                    size_lines = fs_utils::count_lines_in_file(abs_path.string()) + " lines";
                } else {
                    size_lines = ">1MB";
                }
                info = " (" + std::to_string(size_bytes) + " bytes, " + size_lines + ")";
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
    return fs_utils::wrap_prompt_untrusted_data_tag("fs_glob_result", ss.str());
}

} // namespace tools
