#pragma once
#include <string>
#include <memory>
#include "pcre2_regex.h"
#include "../../agentlib/llm_tool.h"

namespace tools {

struct fs_regexp_lines_args {
    std::string path;
    std::string pattern;
    std::string safe_path;
    bool case_insensitive{false};
};

class fs_regexp_lines_tool : public agentlib::llm_tool {
public:
    explicit fs_regexp_lines_tool(fs_regexp_lines_args args);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_regexp_lines_args args_;
    mutable std::unique_ptr<pcre2_regex> compiled_regex_;

    std::string format_line(size_t line_number, const std::string& content) const;
    std::string escape_markdown(const std::string& text) const;
};

} // namespace tools
