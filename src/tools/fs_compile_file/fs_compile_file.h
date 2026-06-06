#pragma once
#include <string>
#include <memory>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/interactions/terminal.h"

namespace tools {

struct fs_compile_file_args {
    std::string path;
    bool async{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_compile_file_args, path, async);

class fs_compile_file_tool : public agentlib::llm_tool {
public:
    fs_compile_file_tool(fs_compile_file_args args, std::string safe_path);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_compile_file_args args_;
    std::string safe_path_;
    std::shared_ptr<agentlib::interaction_terminal> interaction_;
};

class fs_compile_file_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_compile_file"; }
    std::string get_description() const override { return "Compiles a single file and returns the raw console output. Populates the workspace error list. Runs with terminal interaction. NOTE: This only compiles the individual file (e.g. checking syntax/errors) but does NOT link the project, so the executable binary will NOT be updated. To rebuild/link the whole project binary, use fs_compile_project."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "The path to the file to compile, relative to the project root."}
                }},
                {"async", {
                    {"type", "boolean"},
                    {"description", "Optional. If true, starts the compilation in the background."}
                }}
            }},
            {"required", nlohmann::json::array({"path"})}
        };
    }
    
protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
    mutable fs_compile_file_args args_;
    mutable std::string resolved_path_;
};

} // namespace tools
