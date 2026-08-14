#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

class fs_run_tests_tool : public agentlib::llm_tool {
public:
    fs_run_tests_tool(std::vector<std::string> test_names = {}, int timeout = 300);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
    std::vector<std::string> test_names_;
    int timeout_;
};

class fs_run_tests_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_run_tests"; }
    std::string get_description() const override { return "Runs the project's test suite and returns console output. Catch crashes and dumps backtraces. To discover test names, read system://project/testlist.md (or system://project/testlist.md?search=<query>) with fs_read_lines instead of running 'meson test --list' in a shell command."; }
    
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

protected:
    bool validate_args_impl(const nlohmann::json& /*args*/, const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const override {
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override {
        std::vector<std::string> tests;
        if (args.contains("test_names") && args["test_names"].is_array()) {
            for (const auto& t : args["test_names"]) {
                tests.push_back(t.get<std::string>());
            }
        }
        int timeout = 300;
        if (args.contains("timeout") && args["timeout"].is_number_integer()) {
            timeout = args["timeout"].get<int>();
        }
        return std::make_unique<fs_run_tests_tool>(tests, timeout);
    }
};

} // namespace tools