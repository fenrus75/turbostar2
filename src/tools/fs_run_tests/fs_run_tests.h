#pragma once
#include <string>
#include <memory>
#include <span>
#include <vector>
#include "agentlib/llm_tool.h"
#include "agentlib/tool_validator.h"
#include "agentlib/interactions/terminal.h"

namespace tools {

class fs_run_tests_tool : public agentlib::llm_tool {
public:
    fs_run_tests_tool(std::vector<std::string> test_names = {}, int timeout = 300);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

    struct resolve_result {
        std::vector<std::string> resolved_names;
        std::vector<std::string> suggestions;
        std::vector<std::string> auto_matched_notes;
        std::vector<std::string> unresolved_queries;
        bool did_substring_expand{false};
    };
    static resolve_result resolve_test_names_detailed(std::span<const std::string> test_names,
                                                     const std::vector<std::string>& available);

private:
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
    std::vector<std::string> test_names_;
    int timeout_;
};

class fs_run_tests_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_run_tests"; }
    std::string get_description() const override { return "Runs the project's test suite and returns console output. Catch crashes and dumps backtraces. To discover test names, read system://project/testlist.md (or system://project/testlist.md?search=<query>) with fs_read_lines instead of running 'meson test --list' in a shell command.";; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"test_names", {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"description", "Optional list of specific test names to run. If omitted, runs all tests. Full test names can be discovered in system://project/testlist.md; a substring name (e.g. \"run_shell_command\") is expanded to all matching tests."}
                }},
                {"timeout", {
                    {"type", "integer"},
                    {"description", "Optional timeout in seconds. Default is 300."}
                }}
            }}
        };
    }

    std::vector<agentlib::tool_example> get_examples() const override;


protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

private:
    mutable std::vector<std::string> parsed_test_names_;
    mutable int parsed_timeout_{300};
};

} // namespace tools