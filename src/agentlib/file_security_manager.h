#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace agentlib {

enum class access_type {
    read,
    write
};

class file_security_manager {
public:
    file_security_manager();

    // Set the base directory that relative paths should be resolved against
    void set_working_directory(const std::filesystem::path& cwd);

    // Configuration
    void add_allowed_root(const std::filesystem::path& root, access_type max_permission);
    void add_allowed_file(const std::filesystem::path& file, access_type max_permission);
    void add_ignore_pattern(const std::string& pattern);
    
    // Loads patterns from a file like .agentignore
    void load_ignore_file(const std::filesystem::path& ignore_file_path);

    // The core validation method.
    // Takes an arbitrary LLM-provided path (relative or absolute).
    // Returns true if access is allowed.
    // 'out_resolved_path' will contain the fully canonicalized absolute path to use.
    bool validate_access(const std::string& requested_path, 
                         access_type requested_perm, 
                         std::string& out_resolved_path, 
                         std::string& out_error) const;

private:
    struct allowed_path {
        std::filesystem::path path;
        access_type perm;
        bool is_directory;
    };

    std::filesystem::path cwd_;
    std::vector<allowed_path> allowlist_;
    std::vector<std::string> ignore_patterns_;

    bool is_ignored(const std::filesystem::path& path) const;
};

} // namespace agentlib
