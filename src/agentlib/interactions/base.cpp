#include "base.h"
#include "../../markdown_utils.h"
#include <algorithm>
#include <sstream>

namespace agentlib {

int agent_interaction::get_height(int width) const {
    return render(width).size();
}

const std::vector<interaction_line>& agent_interaction::render(int width) const {
    if (width != cached_width_) {
        if (!is_boxed_) {
            cached_lines_ = format_lines(width);
        } else {
            int inner_width = width - 4;
            if (inner_width < 10) inner_width = 10;
            
            std::vector<interaction_line> inner_lines = format_lines(inner_width);
            cached_lines_.clear();
            
            // Single line box drawing characters
            std::string top_left = "\xE2\x94\x8C";
            std::string horiz = "\xE2\x94\x80";
            std::string top_right = "\xE2\x94\x90";
            std::string vert = "\xE2\x94\x82";
            std::string bot_left = "\xE2\x94\x94";
            std::string bot_right = "\xE2\x94\x98";

            std::string top_border = top_left;
            for (int i = 0; i < inner_width + 2; ++i) top_border += horiz;
            top_border += top_right;
            
            interaction_line top_line;
            top_line.text = top_border;
            top_line.color_pair = box_color_pair_;
            cached_lines_.push_back(top_line);
            
            for (const auto& line : inner_lines) {
                int content_len = markdown_utils::utf8_length(line.text);
                int pad_len = inner_width - content_len;
                if (pad_len < 0) pad_len = 0;
                
                interaction_line boxed_line = line;
                boxed_line.prefix = vert + " ";
                boxed_line.prefix_color_pair = box_color_pair_;
                boxed_line.suffix = std::string(pad_len, ' ') + " " + vert;
                boxed_line.suffix_color_pair = box_color_pair_;
                
                cached_lines_.push_back(boxed_line);
            }

            std::string bot_border = bot_left;
            for (int i = 0; i < inner_width + 2; ++i) bot_border += horiz;
            bot_border += bot_right;
            
            interaction_line bot_line;
            bot_line.text = bot_border;
            bot_line.color_pair = box_color_pair_;
            cached_lines_.push_back(bot_line);
        }
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
