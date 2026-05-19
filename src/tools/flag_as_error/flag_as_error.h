#pragma once
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"
#include <nlohmann/json.hpp>
#include <string>

namespace tools {

struct flag_as_error_args {
    std::string safe_path;
    int line;
    int column;
    std::string error_string;
    bool is_warning;
};

class flag_as_error_tool : public agentlib::llm_tool {
public:
    flag_as_error_tool(flag_as_error_args args);
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override { return true; }
    std::string execute(agentlib::tool_context& ctx) override;
private:
    flag_as_error_args args_;
};

class flag_as_error_security : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "flag_as_error"; }
    std::string get_description() const override { return "Flags a specific line in a file as an error or warning, creating an overlay in the editor UI."; }
    nlohmann::json get_parameters_schema() const override;
    
    bool validate_args_impl(
        const nlohmann::json& args, 
        const agentlib::tool_context& ctx, 
        std::string& out_error) const override;
        
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;
};

} // namespace tools
