#pragma once
#include "base.h"

namespace agentlib {

class interaction_tool_call : public agent_interaction {
public:
    explicit interaction_tool_call(std::string text) : text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::tool_call; }
    std::string get_raw_text() const override { return "Tool Call: " + text_; }

    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return "Tool execution"; }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

class interaction_tool_result : public agent_interaction {
public:
    explicit interaction_tool_result(std::string text) : text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::tool_result; }
    std::string get_raw_text() const override { return "Tool Result: " + text_; }
protected:
    std::vector<interaction_line> format_lines(int width) const override;
private:
    std::string text_;
};

} // namespace agentlib
