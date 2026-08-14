#pragma once
#include <string>
#include <vector>
#include <memory>
#include "agentlib/llm_tool.h"

namespace tools {

struct fs_file_codemap_args {
    std::string requested_path;
    std::string safe_path; // Resolved and validated by Stage 1 security
    int min_lines{1};
    bool full{true};
    int max_symbols{0}; // 0 = unlimited / no cap
};

class fs_file_codemap_tool : public agentlib::llm_tool {
public:
    explicit fs_file_codemap_tool(fs_file_codemap_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_file_codemap_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;
};

} // namespace tools
