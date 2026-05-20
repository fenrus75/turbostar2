#include "file_security_manager.h"
#include <fstream>
#include <iostream>

namespace agentlib {

file_security_manager::file_security_manager() {
    // Ensure the default cwd is an absolute, sanitized path
    cwd_ = std::filesystem::weakly_canonical(std::filesystem::current_path());
}

void file_security_manager::set_working_directory(const std::filesystem::path& cwd) {
    cwd_ = std::filesystem::weakly_canonical(cwd);
}

std::filesystem::path file_security_manager::get_working_directory() const {
    return cwd_;
}

void file_security_manager::add_allowed_root(const std::filesystem::path& root, access_type max_permission) {
    allowlist_.push_back({std::filesystem::weakly_canonical(root), max_permission, true});
}

void file_security_manager::add_allowed_file(const std::filesystem::path& file, access_type max_permission) {
    allowlist_.push_back({std::filesystem::weakly_canonical(file), max_permission, false});
}

void file_security_manager::add_ignore_pattern(const std::string& pattern) {
    if (!pattern.empty()) {
        ignore_patterns_.push_back(pattern);
    }
}

void file_security_manager::load_ignore_file(const std::filesystem::path& ignore_file_path) {
    std::ifstream file(ignore_file_path);
    std::string line;
    while (std::getline(file, line)) {
        // Basic trim and comment ignoring
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty() || line[0] == '#') continue;
        add_ignore_pattern(line);
    }
}

bool file_security_manager::is_ignored(const std::filesystem::path& path) const {
    std::string path_str = path.string();
    for (const auto& pattern : ignore_patterns_) {
        // Very basic matching for now: if path contains the pattern string
        // In a full implementation, this should support globbing (e.g. fnmatch or similar)
        if (path_str.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool file_security_manager::validate_access(const std::string& requested_path, 
                                            access_type requested_perm, 
                                            std::string& out_resolved_path, 
                                            std::string& out_error) const {
    
    if (requested_path.starts_with("skills://")) {
        if (requested_perm == access_type::write) {
            out_error = "Virtual files (skills://) are read-only.";
            return false;
        }
        if (vfs_ && (vfs_->exists(requested_path) || !vfs_->list_directory(requested_path).empty())) {
            out_resolved_path = requested_path;
            return true;
        }
        out_error = "Virtual file or directory not found or not mounted.";
        return false;
    }

    std::filesystem::path p(requested_path);
    
    // 1. Resolve to Absolute
    if (p.is_relative()) {
        p = cwd_ / p;
    }

    // 2. Canonicalize
    // weakly_canonical resolves all symlinks, ., and .. for components that exist.
    std::filesystem::path canonical_path;
    try {
        canonical_path = std::filesystem::weakly_canonical(p);
    } catch (const std::exception& e) {
        out_error = "Invalid path format: " + std::string(e.what());
        return false;
    }

    std::string canonical_str = canonical_path.string();

    // 3. Check Ignore List (checked first so an allowed root can't bypass ignores)
    if (is_ignored(canonical_path)) {
        out_error = "Path is excluded by ignore patterns.";
        return false;
    }

    // 4. Check Allowlist
    bool is_allowed = false;
    for (const auto& allowed : allowlist_) {
        // Check permission requirement
        // If requested is write, but allowed is read, skip.
        if (requested_perm == access_type::write && allowed.perm == access_type::read) {
            continue; 
        }

        std::string allowed_str = allowed.path.string();

        if (allowed.is_directory) {
            // Ensure the directory check has a trailing slash to prevent 
            // /allowed_root bypassing into /allowed_root_hacked
            std::string dir_prefix = allowed_str;
            if (!dir_prefix.ends_with(std::filesystem::path::preferred_separator)) {
                dir_prefix += std::filesystem::path::preferred_separator;
            }
            
            // If the requested path is exactly the root dir, or starts with the dir prefix
            if (canonical_str == allowed_str || canonical_str.starts_with(dir_prefix)) {
                is_allowed = true;
                break;
            }
        } else {
            // Exact file match
            if (canonical_str == allowed_str) {
                is_allowed = true;
                break;
            }
        }
    }

    if (!is_allowed) {
        out_error = "Path accesses outside of allowed workspaces or requires higher permissions.";
        return false;
    }

    // 5. Final check against actual on-disk permissions
    // If it exists, check if the OS actually allows the requested access
    if (std::filesystem::exists(canonical_path)) {
        auto perms = std::filesystem::status(canonical_path).permissions();
        if (requested_perm == access_type::read && (perms & std::filesystem::perms::owner_read) == std::filesystem::perms::none) {
            out_error = "Read permission denied by the operating system.";
            return false;
        }
        if (requested_perm == access_type::write && (perms & std::filesystem::perms::owner_write) == std::filesystem::perms::none) {
            out_error = "Write permission denied by the operating system.";
            return false;
        }
    }

    // 6. Success
    out_resolved_path = canonical_str;
    return true;
}

} // namespace agentlib
