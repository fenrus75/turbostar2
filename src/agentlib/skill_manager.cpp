#include "skill_manager.h"
#include <iostream>
#include <cstdlib>

namespace agentlib {

skill_manager& skill_manager::get_instance() {
    static skill_manager instance;
    return instance;
}

virtual_file_system* skill_manager::get_vfs() {
    return vfs_.get();
}

const std::vector<skill>& skill_manager::get_skills() const {
    return skills_;
}

void skill_manager::initialize() {
    const char* home_dir = std::getenv("HOME");
    if (!home_dir) return;

    std::filesystem::path skills_base = std::filesystem::path(home_dir) / ".copilot" / "skills";
    if (!std::filesystem::exists(skills_base) || !std::filesystem::is_directory(skills_base)) {
        return;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(skills_base)) {
            if (entry.is_directory()) {
                std::string skill_name = entry.path().filename().string();
                
                skill new_skill;
                new_skill.name = skill_name;
                new_skill.root_path = entry.path();
                skills_.push_back(new_skill);

                scan_and_mount(entry.path(), skill_name);
            }
        }
    } catch (...) {
        // Silently ignore errors as this is an optional feature
    }
}

void skill_manager::scan_and_mount(const std::filesystem::path& base_dir, const std::string& skill_name) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(base_dir)) {
            if (entry.is_regular_file()) {
                // Compute relative path
                std::string rel_path = std::filesystem::relative(entry.path(), base_dir).string();
                
                // Construct URI: skills://skill_name/rel_path
                std::string uri = "skills://" + skill_name + "/" + rel_path;
                
                vfs_->mount_file(uri, entry.path().string());
            }
        }
    } catch (...) {
        // Silently ignore access errors
    }
}

} // namespace agentlib
