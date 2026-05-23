#pragma once
#include <string>
#include <optional>
#include "../../agentlib/llm_tool.h"

namespace tools {

// Strongly typed C++ arguments. No JSON here.
struct fs_read_lines_args {
    std::string requested_path;
    int start_line; // 1-based index
    int end_line;   // 1-based index
    std::string safe_path; // Injected by Stage 1 validation
};

class fs_read_lines_tool : public agentlib::llm_tool {
public:
    explicit fs_read_lines_tool(fs_read_lines_args args);

    std::shared_ptr<agentlib::agent_interaction> get_interaction() const override;
    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_read_lines_args args_;
    std::shared_ptr<agentlib::agent_interaction> interaction_;

    std::string read_from_document(agentlib::document_snapshot* doc, size_t& out_total_lines) const;
    std::string read_from_disk(size_t& out_total_lines) const;
};

} // namespace tools
