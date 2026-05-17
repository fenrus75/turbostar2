#pragma once
#include <string>
#include "../../agentlib/llm_tool.h"
#include "../../agentlib/tool_validator.h"

namespace tools {

struct fs_write_file_args {
    std::string path;
    std::string content;
    bool force_overwrite;
    std::string safe_path;
};

class fs_write_file_tool : public agentlib::llm_tool {
public:
    explicit fs_write_file_tool(fs_write_file_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_write_file_args args_;
};

class fs_write_file_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_write_file"; }
    std::string get_description() const override { return "Creates a new file or completely overwrites an existing file with the provided content."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "The path to the file to write, relative to the project root."}
                }},
                {"content", {
                    {"type", "string"},
                    {"description", "The entire complete content to write into the file."}
                }},
                {"force_overwrite", {
                    {"type", "boolean"},
                    {"description", "Set to true to overwrite an existing file. Defaults to false."}
                }}
            }},
            {"required", nlohmann::json::array({"path", "content"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override;

private:
    mutable fs_write_file_args args_;
};

} // namespace tools
