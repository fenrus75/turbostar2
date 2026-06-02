#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <re2/re2.h>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/tool_interaction.h"

namespace tools {

struct fs_grep_files_args {
    std::string pattern;
    std::optional<std::string> include_ext;
    std::optional<std::string> dir_path;
    int max_results{50};
    int context_lines{0};
    
    // Resolved safe path
    std::string safe_dir_path;
};

class fs_grep_files_tool : public agentlib::llm_tool {
public:
    explicit fs_grep_files_tool(fs_grep_files_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return interaction_; }
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_grep_files_args args_;
    mutable std::unique_ptr<re2::RE2> compiled_regex_;
    std::shared_ptr<agentlib::interaction_fs_grep_files> interaction_;
    
    std::string escape_markdown(const std::string& text) const;
};

class fs_grep_files_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_grep_files"; }
    std::string get_description() const override { return "Search for a pattern (string or RE2 regular expression) across multiple files in the project. Use this instead of grep. Returns formatted markdown with line numbers and matches."; }
    nlohmann::json get_parameters_schema() const override;
    bool is_pure() const override { return true; }

protected:
    bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable fs_grep_files_args args_;
};

} // namespace tools
