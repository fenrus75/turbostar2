#include "agent_interaction.h"
#include "markdown_utils.h"
#include <algorithm>
#include <sstream>

namespace agentlib {

// Helper to align tables within a string
static std::string align_markdown_tables(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    
    // getline won't capture the last newline if it's empty
    if (!text.empty() && text.back() == '\n') {
        lines.push_back("");
    }

    auto ranges = markdown_utils::table_aligner::find_table_ranges(lines);
    if (ranges.empty()) return text;

    // Process ranges from bottom to top to avoid offset issues
    std::vector<std::string> processed_lines = lines;
    for (auto it = ranges.rbegin(); it != ranges.rend(); ++it) {
        std::vector<std::string> table_block;
        for (size_t i = it->start_line; i <= it->end_line; ++i) {
            table_block.push_back(processed_lines[i]);
        }
        
        auto aligned = markdown_utils::table_aligner::align_table_block(table_block);
        
        // Replace in processed_lines
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

// Helper to wrap text
static std::vector<interaction_line> wrap_text(const std::string& prefix, const std::string& text, int width, int color_pair) {
    std::vector<interaction_line> result;
    std::string current_line = prefix;
    
    size_t start = 0;
    while (start < text.length()) {
        size_t end = text.find('\n', start);
        std::string raw_line = (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);
        
        if (current_line.length() + raw_line.length() <= static_cast<size_t>(width)) {
            current_line += raw_line;
            result.push_back({current_line, color_pair});
            current_line = ""; // Only prefix on the first line usually, or we can add it to all. Let's just prefix the first line.
        } else {
            // Need to wrap
            std::string remaining = raw_line;
            while (!remaining.empty()) {
                size_t chunk_len = width - current_line.length();
                if (chunk_len > remaining.length()) chunk_len = remaining.length();
                
                current_line += remaining.substr(0, chunk_len);
                result.push_back({current_line, color_pair});
                
                remaining = remaining.substr(chunk_len);
                current_line = "";
            }
        }
        
        if (end == std::string::npos) break;
        start = end + 1;
    }
    
    if (!current_line.empty()) {
        result.push_back({current_line, color_pair});
    }
    
    // Always append an empty line as a spacer at the end of each interaction
    result.push_back({"", color_pair});
    
    return result;
}

std::vector<interaction_line> interaction_user_message::format_lines(int width) const {
    // 1 is normal text. We will use a "> " prefix.
    return wrap_text("> ", text_, width, 1);
}

std::vector<interaction_line> interaction_llm_response::format_lines(int width) const {
    // 1 is normal text. No prefix.
    return wrap_text("", align_markdown_tables(text_), width, 1);
}

std::vector<interaction_line> interaction_reasoning::format_lines(int width) const {
    // 10 is muted (White on Green). Let's use it for reasoning too, maybe with a prefix.
    return wrap_text("[Thinking] ", text_, width, 10);
}

std::vector<interaction_line> interaction_tool_call::format_lines(int width) const {
    // 10 is muted (White on Green). Let's use 10 for tool calls so they stand out but aren't normal text.
    return wrap_text("* Executing tool: ", text_, width, 10);
}

std::vector<interaction_line> interaction_tool_result::format_lines(int width) const {
    // 10 is muted.
    return wrap_text("  ↳ Result: ", align_markdown_tables(text_), width, 10);
}

std::vector<interaction_line> interaction_system_message::format_lines(int width) const {
    // 2 is hotkey color (Red on White).
    return wrap_text("[System] ", align_markdown_tables(text_), width, 2);
}

} // namespace agentlib