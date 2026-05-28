#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

struct fs_compile_project_args {
    bool clean{false};
    bool async{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_compile_project_args, clean, async);

class fs_compile_project_tool : public agentlib::llm_tool {
public:
    fs_compile_project_tool(fs_compile_project_args args = {});

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
    fs_compile_project_args args_;
};

class fs_compile_project_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_compile_project"; }
    std::string get_description() const override { return "Compiles the entire project and returns the raw console output. Populates the workspace error list. Runs with terminal interaction."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"clean", {
                    {"type", "boolean"},
                    {"description", "Optional. If true, forces a completely clean rebuild before compiling to clear out stale artifacts."}
                }},
                {"async", {
                    {"type", "boolean"},
                    {"description", "Optional. If true, starts the project compilation in the background."}
                }}
            }}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        try {
            args_ = args.get<fs_compile_project_args>();
            return true;
        } catch (const std::exception& e) {
            out_error = "Invalid arguments: " + std::string(e.what());
            return false;
        }
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<fs_compile_project_tool>(args_);
    }
    
private:
    mutable fs_compile_project_args args_;
};

} // namespace tools
