#include "fs_replace_content.h"
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/ai_agent.h"

namespace tools {

bool fs_replace_content_validator::is_allowed_in_plan_mode(const nlohmann::json& args, const agentlib::tool_context& ctx) const {
    if (!ctx.active_agent) return false;
    if (!args.contains("path") || !args["path"].is_string()) return false;
    std::string plan_file = ctx.active_agent->get_plan_file();
    return !plan_file.empty() && args["path"].get<std::string>() == plan_file;
}

bool fs_replace_content_validator::validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const {
    try {
        if (!raw_args.contains("path") || !raw_args["path"].is_string()) {
            out_error = "Missing or invalid 'path' parameter.";
            return false;
        }
        std::string raw_path = raw_args["path"].get<std::string>();
        if (raw_path.find("..") != std::string::npos) {
            out_error = "Path cannot contain '..' directory traversal.";
            return false;
        }

        if (!raw_args.contains("target_content") || !raw_args["target_content"].is_string()) {
            out_error = "Missing or invalid 'target_content' parameter.";
            return false;
        }
        std::string target = raw_args["target_content"].get<std::string>();
        if (target.empty()) {
            out_error = "'target_content' parameter cannot be empty.";
            return false;
        }

        if (!raw_args.contains("replacement_content") || !raw_args["replacement_content"].is_string()) {
            out_error = "Missing or invalid 'replacement_content' parameter.";
            return false;
        }
        std::string replacement = raw_args["replacement_content"].get<std::string>();

        std::optional<int> hint;
        if (raw_args.contains("line_hint")) {
            if (!raw_args["line_hint"].is_number_integer()) {
                out_error = "Invalid 'line_hint' parameter (must be an integer).";
                return false;
            }
            int h_val = raw_args["line_hint"].get<int>();
            if (h_val < 1) {
                out_error = "'line_hint' must be a positive 1-based integer.";
                return false;
            }
            hint = h_val;
        }

        // Perform file security manager check (write access)
        std::string canonical_path;
        if (!ctx.fs_security.validate_access(raw_path, agentlib::access_type::write, canonical_path, out_error)) {
            return false;
        }

        args_.path = raw_path;
        args_.safe_path = canonical_path;
        args_.target_content = target;
        args_.replacement_content = replacement;
        args_.line_hint = hint;

        return true;
    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> fs_replace_content_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
    return std::make_unique<fs_replace_content_tool>(args_);
}

REGISTER_TOOL(fs_replace_content_validator)

} // namespace tools
