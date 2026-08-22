#include "fs_replace_content.h"
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/ai_agent.h"

namespace tools {

std::vector<agentlib::tool_example> fs_replace_content_validator::get_examples() const {
    return {
        {
            "Function-Scoped Replacement",
            nlohmann::json{
                {"path", "src/main.cpp"},
                {"function_hint", "parse_config"},
                {"target_content", "int timeout = 5;"},
                {"replacement_content", "int timeout = 10;"}
            },
            "Replaces target lines strictly within the specified function scope."
        }
    };
}

bool fs_replace_content_validator::validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const {

    try {
        if (!raw_args.contains("path") || !raw_args["path"].is_string()) {
            out_error = "Missing or invalid 'path' parameter.";
            return false;
        }
        std::string raw_path = raw_args["path"].get<std::string>();

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

        std::optional<std::string> func_hint;
        if (raw_args.contains("function_hint")) {
            if (!raw_args["function_hint"].is_string()) {
                out_error = "Invalid 'function_hint' parameter (must be a string).";
                return false;
            }
            std::string fh_val = raw_args["function_hint"].get<std::string>();
            if (!fh_val.empty()) {
                func_hint = fh_val;
            }
        }

        std::string check_path = raw_path;
        if (check_path.starts_with("file://")) {
            check_path = check_path.substr(7);
        }

        // Perform file security manager check (write access)
        std::string canonical_path;
        if (!ctx.fs_security.validate_access(check_path, agentlib::access_type::write, canonical_path, out_error)) {
            return false;
        }

        args_.path = raw_path;
        args_.safe_path = canonical_path;
        args_.target_content = target;
        args_.replacement_content = replacement;
        args_.line_hint = hint;
        args_.function_hint = func_hint;

        std::optional<int> start_l;
        if (raw_args.contains("start_line")) {
            if (!raw_args["start_line"].is_number_integer()) {
                out_error = "Invalid 'start_line' parameter (must be an integer).";
                return false;
            }
            int sl_val = raw_args["start_line"].get<int>();
            if (sl_val < 1) {
                out_error = "'start_line' must be a positive 1-based integer.";
                return false;
            }
            start_l = sl_val;
        }

        std::optional<int> end_l;
        if (raw_args.contains("end_line")) {
            if (!raw_args["end_line"].is_number_integer()) {
                out_error = "Invalid 'end_line' parameter (must be an integer).";
                return false;
            }
            int el_val = raw_args["end_line"].get<int>();
            if (el_val < 1) {
                out_error = "'end_line' must be a positive 1-based integer.";
                return false;
            }
            if (start_l && el_val < *start_l) {
                out_error = "'end_line' must be greater than or equal to 'start_line'.";
                return false;
            }
            end_l = el_val;
        }

        args_.start_line = start_l;
        args_.end_line = end_l;

        if (raw_args.contains("strict")) {
            if (!raw_args["strict"].is_boolean()) {
                out_error = "Invalid 'strict' parameter (must be a boolean).";
                return false;
            }
            args_.strict = raw_args["strict"].get<bool>();
        }

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
