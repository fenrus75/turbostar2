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
        ss << "| " << s.name << " | " << s.uri << " | " << s.description << " |\n";
    }

    return ss.str();
}

} // namespace tools
