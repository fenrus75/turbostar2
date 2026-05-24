#pragma once
#include <string>
#include <memory>
#include "../../agentlib/single_string_tool_validator.h"

namespace tools {

class web_fetch_tool : public agentlib::llm_tool {
public:
    explicit web_fetch_tool(std::string url);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    std::string url_;
    std::string domain_;
};

class web_fetch_validator : public agentlib::single_string_tool_validator {
public:
    std::string get_name() const override { return "web_fetch"; }
    std::string get_description() const override { return "Fetches content from a URL via HTTP/HTTPS. Useful for reading documentation or external resources."; }
    std::string get_parameter_name() const override { return "url"; }
    std::string get_parameter_description() const override { return "The full URL to fetch (must start with http:// or https://)."; }

protected:
    bool validate_string_arg(const std::string& arg, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string& arg) const override;
};

} // namespace tools