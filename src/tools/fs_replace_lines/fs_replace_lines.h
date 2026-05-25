#pragma once
#include <string>
#include <vector>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct edit_operation {
    int line_number;
    std::string type; // "add", "remove", "replace"
    std::string original_text; // Empty if not used
    std::string replace_with; // Empty if not used
    int lines_to_remove{1}; // Calculated from newlines in original_text
};

struct fs_replace_args {
    std::string path;
    std::string safe_path;
    std::vector<edit_operation> edits;
};

class fs_replace_lines_tool : public agentlib::llm_tool {
public:
    explicit fs_replace_lines_tool(fs_replace_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_replace_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
    
    std::string execute_disk_fallback(agentlib::tool_context& ctx);
};

class fs_replace_lines_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_replace_lines"; }
    std::string get_description() const override { return "Surgically edit a file by providing an array of line operations (add, remove, replace). Edits MUST be sorted in descending line_number order (strictly bottom to top)."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "Path to the file to edit, relative to the project root."}
                }},
                {"edits", {
                    {"type", "array"},
                    {"description", "A list of edit operations. Operations MUST be sorted by line_number in DESCENDING order (strictly bottom to top, e.g. edit line 100, then line 50) to prevent line shifting."},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"line_number", {
                                {"type", "integer"},
                                {"description", "The 1-based line number to target."}
                            }},
                            {"type", {
                                {"type", "string"},
                                {"enum", nlohmann::json::array({"add", "remove", "replace"})},
                                {"description", "The type of edit operation."}
                            }},
                            {"original_text", {
                                {"type", "string"},
                                {"description", "Required for 'remove' and 'replace'. The exact full content of the original line(s) being modified. You MAY provide multiple lines separated by \\n to replace entire blocks of code. Used for safety verification. Pass empty string for 'add'."}
                            }},
                            {"replace_with", {
                                {"type", "string"},
                                {"description", "Required for 'add' and 'replace'. The new content to insert or replace the line with. You MAY use newline characters (\\n) here to insert multiple lines. Pass empty string for 'remove'."}
                            }}
                        }},
                        {"required", nlohmann::json::array({"line_number", "type"})}
                    }}
                }}
            }},
            {"required", nlohmann::json::array({"path", "edits"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
    mutable fs_replace_args args_;
};

} // namespace tools
