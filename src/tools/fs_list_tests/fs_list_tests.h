#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include <re2/re2.h>

namespace tools {

struct fs_list_tests_args {
    std::string pattern;
};

class fs_list_tests_tool : public agentlib::llm_tool {
public:
    explicit fs_list_tests_tool(fs_list_tests_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override { return nullptr; }
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_list_tests_args args_;
};

class fs_list_tests_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_list_tests"; }
    std::string get_description() const override { return "Returns a markdown table of available test names in the project, optionally filtered by a pattern."; }
    bool is_pure() const override { return true; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pattern", {
                    {"type", "string"},
                    {"description", "Optional pattern (string or RE2 regular expression) to filter test names."}
                }}
            }}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        try {
            for (auto it = raw_args.begin(); it != raw_args.end(); ++it) {
                if (it.key() != "pattern") {
                    out_error = "Unexpected argument: " + it.key();
                    return false;
                }
            }
            
            args_.pattern = raw_args.value("pattern", "");
            if (!args_.pattern.empty()) {
                // Pre-compile with RE2 to validate it's a valid regex
                re2::RE2 re(args_.pattern);
                if (!re.ok()) {
                    out_error = "Invalid regular expression pattern: " + re.error();
                    return false;
                }
            }
        } catch (const std::exception& e) {
            out_error = "Failed to parse parameters: " + std::string(e.what());
            return false;
        }
        return true;
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<fs_list_tests_tool>(args_);
    }

private:
    mutable fs_list_tests_args args_;
};

} // namespace tools
