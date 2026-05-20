#include "activate_skill.h"
#include <sstream>

namespace tools {

activate_skill_tool::activate_skill_tool(activate_skill_args args) : args_(std::move(args)) {}

bool activate_skill_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true; // Name validation was already done in security phase
}

std::string activate_skill_tool::execute(agentlib::tool_context& ctx) {
    auto vfs = ctx.fs_security.get_vfs();
    if (!vfs) {
        return "Error: Virtual File System not initialized.";
    }

    std::string skill_md_uri = args_.target_skill.uri + "SKILL.md";
    auto view_opt = vfs->read_file(skill_md_uri);
    
    std::string instructions;
    if (view_opt) {
        instructions = std::string(view_opt.value());
    } else {
        instructions = "Error: SKILL.md not found in skill root.";
    }

    std::stringstream ss;
    ss << "<ACTIVATED_SKILL name=\"" << args_.name << "\">\n";
    ss << "<INSTRUCTIONS>\n" << instructions << "\n</INSTRUCTIONS>\n";
    ss << "<AVAILABLE_RESOURCES>\n";
    
    auto entries = vfs->list_directory(args_.target_skill.uri);
    for (const auto& entry : entries) {
        // Strip the base URI prefix to just show the relative path
        std::string filename = entry.uri.substr(args_.target_skill.uri.length());
        if (filename.empty()) continue;
        
        // Print it back as a full URI so the agent knows exactly how to request it
        ss << args_.target_skill.uri << filename;
        if (entry.type == 'D') ss << "/";
        ss << "\n";
    }
    
    ss << "</AVAILABLE_RESOURCES>\n";
    ss << "</ACTIVATED_SKILL>";

    return ss.str();
}

} // namespace tools
