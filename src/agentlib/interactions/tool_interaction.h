#pragma once
#include "base.h"

namespace agentlib {

class interaction_tool_call : public agent_interaction {
public:
    explicit interaction_tool_call(std::string tool_name, std::string text) : tool_name_(std::move(tool_name)), text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::tool_call; }
    interaction_role get_role() const override { return interaction_role::thinking; }
    std::string get_raw_text() const override { return "Tool Call: " + text_; }
    std::string get_grouping_key() const override { return tool_name_; }

    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return "Tool execution"; }
protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;
private:
    std::string tool_name_;
    std::string text_;
};

class interaction_tool_result : public agent_interaction {
public:
    explicit interaction_tool_result(std::string tool_name, std::string text) : tool_name_(std::move(tool_name)), text_(std::move(text)) {}
    interaction_type get_type() const override { return interaction_type::tool_result; }
    interaction_role get_role() const override { return interaction_role::agent; }
    std::string get_raw_text() const override { return "Tool Result: " + text_; }
    std::string get_grouping_key() const override { return tool_name_; }
protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;
private:
    std::string tool_name_;
    std::string text_;
};

class interaction_fs_grep_files : public agent_interaction {
public:
    explicit interaction_fs_grep_files(std::string pattern, std::string dir_path)
        : pattern_(std::move(pattern)), dir_path_(std::move(dir_path))
    {
        if (dir_path_.empty()) {
            result_ = "Searching for \"" + pattern_ + "\"...";
        } else {
            result_ = "Searching for \"" + pattern_ + "\" in " + dir_path_ + "...";
        }
    }
    interaction_type get_type() const override { return interaction_type::tool_result; }
    interaction_role get_role() const override { return interaction_role::agent; }
    std::string get_raw_text() const override { return "Search Results for " + pattern_; }
    std::string get_grouping_key() const override { return "fs_grep_files"; }
    
    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return "Search: " + pattern_; }

    void set_result(const std::string& result) {
        result_ = result;
        invalidate_cache();
    }
protected:
    std::vector<interaction_line> format_lines(int width, background_mode bg) const override;
private:
    std::string pattern_;
    std::string dir_path_;
    std::string result_;
};

} // namespace agentlib
