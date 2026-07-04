#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include "virtual_file_system.h"

#include <mutex>
#include <map>

namespace agentlib {

class ai_agent;

struct skill {
    std::string name;
    std::string description;
    std::string uri;
    bool visible = true;
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

    // Registers a skill dynamically
    void register_skill(const std::string &name, const std::string &description, const std::string &uri, bool visible = true);

    // Registers a skill dynamically from single SKILL.md content
    void register_skill(const std::string &skill_content, bool visible = false);

    // Registers a skill dynamically from a map of relative filenames to contents
    void register_skill(const std::map<std::string, std::string> &files, bool visible = false);

    // Toggles visibility of a registered skill
    void set_visibility(const std::string &name, bool visible);

    // Formats a skill's content to XML <skill_content> block
    std::string format_skill_content(const std::string &name) const;

    // Unregisters a skill dynamically
    void unregister_skill(const std::string &name);

private:
    skill_manager() = default;
    ~skill_manager() = default;

    void scan_and_mount(const std::filesystem::path& base_dir, const std::string& skill_name);

    std::unique_ptr<virtual_file_system> vfs_{std::make_unique<virtual_file_system>()};
    /*
     * mutex_ protects the skills_ vector during dynamic registration,
     * visibility updates, and initialization.
     * Locking Rules:
     * - Held briefly when modifying or reading skills_ in thread-safe contexts.
     */
    mutable std::mutex mutex_;
    std::vector<skill> skills_;
};

} // namespace agentlib
