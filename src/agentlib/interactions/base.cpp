#include "base.h"
#include <algorithm>
#include <sstream>

namespace agentlib {

int agent_interaction::get_height(int width) const {
    return render(width).size();
}

const std::vector<interaction_line>& agent_interaction::render(int width) const {
    if (width != cached_width_) {
        cached_lines_ = format_lines(width);
        cached_width_ = width;
    }
    return cached_lines_;
}

std::vector<interaction_line> agent_interaction::wrap_text(const std::string& prefix, const std::string& text, int width, int color_pair) {
    std::vector<interaction_line> lines;
    if (width <= static_cast<int>(prefix.length()) + 5) {
        lines.push_back({prefix + text, color_pair});
        return lines;
    }

    std::string full_text = text;
    std::stringstream ss(full_text);
    std::string line;
    bool first = true;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string current_prefix = first ? prefix : std::string(prefix.length(), ' ');
        int available_width = width - current_prefix.length();

        if (line.empty()) {
            lines.push_back({current_prefix, color_pair});
            first = false;
            continue;
        }

        size_t start = 0;
        while (start < line.length()) {
            size_t len = std::min(static_cast<size_t>(available_width), line.length() - start);
            
            // Try to break at space if not at end of line
            if (start + len < line.length()) {
                size_t space = line.find_last_of(" \t", start + len);
                if (space != std::string::npos && space > start) {
                    len = space - start;
                }
            }

            lines.push_back({current_prefix + line.substr(start, len), color_pair});
            start += len;
            while (start < line.length() && (line[start] == ' ' || line[start] == '\t')) {
                start++;
            }
            current_prefix = std::string(prefix.length(), ' ');
            available_width = width - current_prefix.length();
            first = false;
        }
    }
    
    if (lines.empty()) {
        lines.push_back({prefix, color_pair});
    }

    return lines;
}

} // namespace agentlib
