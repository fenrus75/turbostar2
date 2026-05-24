#pragma once

#include <string>
#include <vector>
#include <memory>

namespace agentlib {

enum class interaction_type {
    system_message,
    user_message,
    reasoning,
    llm_response,
    tool_call,
    tool_result,
    terminal,
    action,
    unknown
};

struct interaction_line {
    std::string text;
    int color_pair;
    std::string prefix = "";
    int prefix_color_pair = 0;
    std::string suffix = "";
    int suffix_color_pair = 0;
};

class agent_interaction {
public:
    virtual ~agent_interaction() = default;

    virtual interaction_type get_type() const = 0;

    int get_height(int width) const;
    const std::vector<interaction_line>& render(int width) const;
    virtual std::string get_raw_text() const = 0;

    void invalidate_cache() { cached_width_ = -1; }

    bool is_boxed() const { return is_boxed_; }
    void set_boxed(bool boxed, int color_pair = 5, const std::string& title = "") {
        is_boxed_ = boxed;
        box_color_pair_ = color_pair;
        box_title_ = title;
        invalidate_cache();
    }

    int get_age() const { return age_; }
    virtual void set_age(int age) { 
        if (age_ != age) {
            age_ = age; 
            invalidate_cache(); 
        }
    }

    // Sub-panel metadata for grouped rendering
    virtual bool needs_subpanel_header() const { return false; }
    virtual std::string get_subpanel_label() const { return ""; }
    virtual bool can_merge_with_previous(const agent_interaction& previous) const;

protected:
    virtual std::vector<interaction_line> format_lines(int width) const = 0;

    // Helper for wrapping text with optional prefix
    static std::vector<interaction_line> wrap_text(const std::string& prefix, const std::string& text, int width, int color_pair);

private:
    mutable int cached_width_{-1};
    mutable std::vector<interaction_line> cached_lines_;
    bool is_boxed_ = false;
    int box_color_pair_ = 5;
    std::string box_title_;
    int age_ = 0;
};

} // namespace agentlib
