#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include "virtual_file_system.h"

namespace agentlib {

struct skill {
    std::string name;
    std::string description;
    std::filesystem::path root_path;
};

class skill_manager {
public:
    static skill_manager& get_instance();

    // Prevent copy/move
    skill_manager(const skill_manager&) = delete;
    skill_manager& operator=(const skill_manager&) = delete;

    // Scans ~/.copilot/skills/ and populates the VFS
    void initialize();

    virtual_file_system* get_vfs();
    
    const std::vector<skill>& get_skills() const;

private:
    skill_manager() = default;
    ~skill_manager() = default;

    void scan_and_mount(const std::filesystem::path& base_dir, const std::string& skill_name);

    std::unique_ptr<virtual_file_system> vfs_{std::make_unique<virtual_file_system>()};
    std::vector<skill> skills_;
};

} // namespace agentlib
