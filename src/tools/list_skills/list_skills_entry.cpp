#include "list_skills.h"
#include "../../agentlib/skill_manager.h"
#include <sstream>

namespace tools {

bool list_skills_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string list_skills_tool::execute(agentlib::tool_context& /*ctx*/) {
    auto& skills = agentlib::skill_manager::get_instance().get_skills();
    
    if (skills.empty()) {
        return "No skills are currently available.";
    }

    std::stringstream ss;
    ss << "| Skill Name | URI | Description |\n";
    ss << "| ---------- | --- | ----------- |\n";
    
    for (const auto& s : skills) {
        // Simple sanitization for markdown table (replacing newlines with spaces)
        std::string desc_sanitized = s.description;
        for (char& c : desc_sanitized) {
            if (c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        
        ss << "| " << s.name << " | " << s.uri << " | " << desc_sanitized << " |\n";
    }

    return ss.str();
}

} // namespace tools
