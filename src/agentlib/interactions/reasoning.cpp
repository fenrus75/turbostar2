#include "reasoning.h"
#include "../../markdown_utils.h"
#include <sstream>

namespace agentlib {

// Duplicate helper for reasoning blocks as well
static std::string align_markdown_tables(const std::string& text, bool framed = false) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    
    if (!text.empty() && text.back() == '\n') {
        lines.push_back("");
    }

    auto ranges = markdown_utils::table_aligner::find_table_ranges(lines);
    if (ranges.empty()) return text;

    std::vector<std::string> processed_lines = lines;
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
        std::vector<std::string> table_block;
        for (size_t i = it->start_line; i <= it->end_line; ++i) {
            table_block.push_back(processed_lines[i]);
        }
        
        markdown_utils::align_options opts;
        opts.use_utf8_frames = framed;
        auto aligned = markdown_utils::table_aligner::align_table_block(table_block, opts);
        
        processed_lines.erase(processed_lines.begin() + it->start_line, processed_lines.begin() + it->end_line + 1);
        processed_lines.insert(processed_lines.begin() + it->start_line, aligned.begin(), aligned.end());
    }

    std::string result;
    for (size_t i = 0; i < processed_lines.size(); ++i) {
        result += processed_lines[i];
        if (i < processed_lines.size() - 1) result += "\n";
    }
    return result;
}

std::vector<interaction_line> interaction_reasoning::format_lines(int width) const {
    if (get_age() > 5) {
        // Return a single line separator spanning the width
        std::vector<interaction_line> lines;
        std::string horiz = "\xE2\x94\x80"; // ─
        std::string title = " Agent Reasoning ";
        int title_len = markdown_utils::utf8_length(title);
        
        std::string line_text;
        if (title_len >= width) {
            for (int i = 0; i < width; ++i) line_text += horiz;
        } else {
            int left_pad = (width - title_len) / 2;
            int right_pad = width - title_len - left_pad;
            for (int i = 0; i < left_pad; ++i) line_text += horiz;
            line_text += title;
            for (int i = 0; i < right_pad; ++i) line_text += horiz;
        }
        lines.push_back({line_text, 10}); // 10 is White on Green
        return lines;
    }

    std::string display_text = text_;
    if (get_age() > 0) {
        size_t pos = 0;
        int lines = 0;
        while (lines < 3 && pos < display_text.length()) {
            pos = display_text.find('\n', pos);
            if (pos == std::string::npos) {
                pos = display_text.length();
                break;
            }
            pos++;
            lines++;
        }
        if (pos < display_text.length()) {
            display_text = display_text.substr(0, pos);
            if (!display_text.empty() && display_text.back() == '\n') {
                display_text.pop_back(); // Remove trailing newline to append ellipsis cleanly
            }
            display_text += "\n... (reasoning collapsed)";
        }
    }
    return wrap_text("", align_markdown_tables(display_text, false), width, 10);
}

} // namespace agentlib
