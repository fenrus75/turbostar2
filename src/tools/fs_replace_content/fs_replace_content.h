#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct fs_replace_content_args {
    std::string path;
    std::string safe_path;
    std::string target_content;
    std::string replacement_content;
    std::optional<int> line_hint;
    std::optional<std::string> function_hint;
    std::optional<int> start_line;
    std::optional<int> end_line;
    bool strict{false}; // If true, reject (and revert) edits that leave braces unbalanced instead of warning.
};

class fs_replace_content_tool : public agentlib::llm_tool {
public:
    explicit fs_replace_content_tool(fs_replace_content_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_replace_content_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;

    std::string execute_disk_fallback(agentlib::tool_context& ctx);
};

class fs_replace_content_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_replace_content"; }
    std::string get_description() const override { 
        return "Edit a file by replacing a unique contiguous block of text (target_content) with a new block (replacement_content). Avoids line-number shifts."; 
    }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "Path to the file to edit, relative to the project root."}
                }},
                {"target_content", {
                    {"type", "string"},
                    {"description", "The exact, contiguous block of text in the file to be replaced. Must match exactly."}
                }},
                {"replacement_content", {
                    {"type", "string"},
                    {"description", "The new text block that will replace the target_content."}
                }},
                {"line_hint", {
                    {"type", "integer"},
                    {"description", "Optional. A 1-based line number representing a hint of where target_content is located in the file. Useful if there could be multiple matching blocks."}
                }},
                {"function_hint", {
                    {"type", "string"},
                    {"description", "Optional. The name of the enclosing function, method, or class (e.g. 'execute_disk_fallback'). Highly recommended for long files to restrict search scope and resolve ambiguity."}
                }},
                {"start_line", {
                    {"type", "integer"},
                    {"description", "Optional. A 1-based start line number establishing a search window boundary for target_content."}
                }},
                {"end_line", {
                    {"type", "integer"},
                    {"description", "Optional. A 1-based end line number establishing a search window boundary for target_content."}
                }},
                {"strict", {
                    {"type", "boolean"},
                    {"description", "Optional. If true, reject (and revert) the edit when it would leave braces unbalanced, instead of applying it and only issuing a warning. Defaults to false."}
                }}
            }},
            {"required", nlohmann::json::array({"path", "target_content", "replacement_content"})}
        };
    }
    std::vector<agentlib::tool_example> get_examples() const override;

protected:

    bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable fs_replace_content_args args_;
};

} // namespace tools
