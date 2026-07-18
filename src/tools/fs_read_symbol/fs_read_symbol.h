#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../../agentlib/llm_tool.h"

namespace tools {

struct fs_read_symbol_args {
    std::string requested_path;
    std::string symbol_name;
    std::string safe_path; // Resolved and validated by Stage 1
};

class fs_read_symbol_tool : public agentlib::llm_tool {
public:
    explicit fs_read_symbol_tool(fs_read_symbol_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_read_symbol_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;

    std::vector<std::string> read_lines(const std::string& path, int start, int end, agentlib::tool_context& ctx) const;
};

} // namespace tools
