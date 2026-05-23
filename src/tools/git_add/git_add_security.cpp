#include "git_add.h"
#include "../../agentlib/tool_registry.h"

namespace tools {

bool git_add_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const {
    resolved_paths_.clear();
    
    if (!args.contains("paths") || !args["paths"].is_array()) {
        out_error = "Missing or invalid 'paths' array.";
        return false;
    }

    if (args["paths"].empty()) {
        out_error = "The 'paths' array cannot be empty.";
        return false;
    }

    for (const auto& path_val : args["paths"]) {
        if (!path_val.is_string()) {
            out_error = "All items in 'paths' must be strings.";
            return false;
        }
        
        std::string raw_path = path_val.get<std::string>();
        std::string resolved_path;
        
        // Stage 1 Security: Validate against the file_security_manager.
        // Even though git add doesn't strictly 'read' the file into our process,
        // it exposes file metadata to the git index, so we require read permission.
        if (!ctx.fs_security.validate_access(raw_path, agentlib::access_type::read, resolved_path, out_error)) {
            out_error = "Access denied for path '" + raw_path + "': " + out_error;
            return false;
        }
        
        resolved_paths_.push_back(resolved_path);
    }

    return true;
}

std::unique_ptr<agentlib::llm_tool> git_add_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
    return std::make_unique<git_add_tool>(resolved_paths_);
}

REGISTER_TOOL(git_add_validator)

} // namespace tools
