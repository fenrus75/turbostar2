#include "code_get_definition.h"
#include "../../lsp_manager.h"
#include "../../fs_utils.h"

namespace tools {

std::string code_get_definition_tool::execute(agentlib::tool_context& ctx) {
    std::string safe_path;
    std::string error;
    if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, safe_path, error)) {
        return "Error: " + error;
    }

    if (!lsp_manager::get_instance().is_supported_file(safe_path)) {
        return "Error: LSP is not supported for this file type.";
    }

    auto locations = lsp_manager::get_instance().query_definition(safe_path, args_.line - 1, args_.character);
    if (locations.empty()) {
        return "No definition found.";
    }

    nlohmann::json result = nlohmann::json::array();
    for (const auto& loc : locations) {
        // Resolve path relative to project root if possible for cleaner output
        std::string display_path = loc.path;
        
        result.push_back({
            {"path", display_path},
            {"start_line", loc.range.start_y + 1},
            {"start_character", loc.range.start_x},
            {"end_line", loc.range.end_y + 1},
            {"end_character", loc.range.end_x}
        });
    }

    return result.dump(2);
}

class code_get_definition_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "code_get_definition"; }
    std::string get_description() const override { 
        return "Finds the definition(s) of a symbol at a specific location."; 
    }
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "The file path."}}},
                {"line", {{"type", "integer"}, {"description", "The 1-based line number."}}},
                {"character", {{"type", "integer"}, {"description", "The 0-based character offset."}}}
            }},
            {"required", {"path", "line", "character"}}
        };
    }

    bool is_pure() const override { return true; }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        if (!args.is_object() || !args.contains("path") || !args.contains("line") || !args.contains("character")) {
            out_error = "Missing required arguments.";
            return false;
        }
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override {
        code_get_definition_args parsed_args;
        parsed_args.path = args["path"].get<std::string>();
        parsed_args.line = args["line"].get<int>();
        parsed_args.character = args["character"].get<int>();
        return std::make_unique<code_get_definition_tool>(std::move(parsed_args));
    }
};

REGISTER_TOOL(code_get_definition_validator);

} // namespace tools
