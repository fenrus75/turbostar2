#include "action.h"

namespace agentlib {

interaction_action::interaction_action(std::string action_text) 
    : action_text_(std::move(action_text)) {
}

void interaction_action::set_status(status s, const std::string& result_message) {
    status_ = s;
    result_text_ = result_message;
    invalidate_cache();
}

void interaction_action::set_action_text(const std::string& text) {
    action_text_ = text;
    invalidate_cache();
}

std::string interaction_action::get_raw_text() const {
    std::string icon;
    if (status_ == status::pending) icon = "\xE2\x97\x8F"; // ●
    else if (status_ == status::success) icon = "\xE2\x9C\x94"; // ✔
    else icon = "\xE2\x9C\x96"; // ✖
    
    std::string raw = icon + "  " + action_text_;
    if (!result_text_.empty()) {
        raw += " \xE2\x86\x92 " + result_text_;
    }
    return raw;
}

std::vector<interaction_line> interaction_action::format_lines(int width) const {
    int color = 33; // 33 is Bright Yellow on Cyan (Pending)
    if (status_ == status::success) color = 8; // 8 is Bright White on Cyan
    else if (status_ == status::failure) color = 35; // 35 is Bright Red on Cyan
    
    auto lines = wrap_text("", get_raw_text(), width, color);
    
    auto extra_lines = format_extra_lines(width);
    if (!extra_lines.empty()) {
        lines.insert(lines.end(), extra_lines.begin(), extra_lines.end());
    }
    
    return lines;
}

} // namespace agentlib
