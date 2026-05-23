#include "tool_interaction.h"
#include "../../markdown_utils.h"
#include <sstream>

namespace agentlib {

// Duplicate helper for tool interactions as well
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

std::vector<interaction_line> interaction_tool_call::format_lines(int width) const {
    return wrap_text("* Executing tool: ", align_markdown_tables(text_, true), width, 10);
}

std::vector<interaction_line> interaction_tool_result::format_lines(int width) const {
    return wrap_text("  ↳ Result: ", align_markdown_tables(text_, true), width, 10);
}

} // namespace agentlib
