#pragma once
#include <string>
#include <optional>
#include <vector>
#include "../../agentlib/llm_tool.h"

namespace agentlib {
class document_snapshot;
class virtual_file_system;
}

namespace tools {

struct fs_read_lines_args {
    std::string requested_path;
    int start_line; // 1-based index
    int end_line;   // 1-based index
    std::optional<int> tail;
    std::optional<int> length;
    std::string safe_path; // Injected by Stage 1 validation
};

struct file_read_result {
    bool success = false;
    std::string error_message;
    std::vector<std::string> lines;
    size_t total_file_lines = 0;
    int start_line = 1;
    int end_line = 1;
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

    file_read_result read_from_document(agentlib::document_snapshot* doc, int start, int end) const;
    file_read_result read_from_disk(const std::string& path, int start, int end) const;
    file_read_result read_from_vfs(agentlib::virtual_file_system* vfs, const std::string& path, int start, int end) const;
};

} // namespace tools
