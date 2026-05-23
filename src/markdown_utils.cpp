#include "markdown_utils.h"
#include <algorithm>
#include <sstream>
#include <iostream>

namespace markdown_utils {

bool table_aligner::is_table_row(const std::string& line) {
    // A table row must have at least one pipe character
    if (line.find('|') == std::string::npos) return false;
    
    // It shouldn't be just whitespace and pipes
    bool has_content = false;
    for (char c : line) {
        if (!std::isspace(c) && c != '|') {
            has_content = true;
            break;
        }
    }
    return has_content;
}

bool table_aligner::is_header_separator(const std::string& line) {
    if (line.find('|') == std::string::npos) return false;
    
    // A header separator contains only pipes, dashes, colons, and whitespace
    for (char c : line) {
        if (!std::isspace(c) && c != '|' && c != '-' && c != ':') {
            return false;
        }
    }
    
    // Must have at least some dashes
    return line.find('-') != std::string::npos;
}

std::vector<table_range> table_aligner::find_table_ranges(const std::vector<std::string>& lines) {
    std::vector<table_range> ranges;
    
    size_t i = 0;
    while (i < lines.size()) {
        if (is_table_row(lines[i])) {
            // Potential table start. A valid table must have a header separator.
            size_t potential_start = i;
            bool found_separator = false;
            size_t end_line = i;
            
            size_t j = i;
            while (j < lines.size() && is_table_row(lines[j])) {
                if (is_header_separator(lines[j])) {
                    found_separator = true;
                }
                end_line = j;
                j++;
            }
            
            if (found_separator) {
                ranges.push_back({potential_start, end_line});
                i = end_line + 1;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
    
    return ranges;
}

std::vector<std::string> table_aligner::tokenize_row(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool escaped = false;
    
    // Handle rows that start and end with pipes
    std::string trimmed = trim(line);
    size_t start = 0;
    size_t end = trimmed.length();
    if (!trimmed.empty() && trimmed.front() == '|') start = 1;
    if (trimmed.length() > 1 && trimmed.back() == '|') end = trimmed.length() - 1;
    
    for (size_t i = start; i < end; ++i) {
        char c = trimmed[i];
        if (escaped) {
            current += c;
            escaped = false;
        } else if (c == '\\') {
            current += c;
            escaped = true;
        } else if (c == '|') {
            tokens.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(trim(current));
    return tokens;
}

std::string table_aligner::trim(const std::string& s) {
    auto first = s.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) return "";
    auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, (last - first + 1));
}

size_t table_aligner::utf8_length(const std::string& s) {
    size_t offset = 0;
    size_t chars = 0;
    while (offset < s.length()) {
        unsigned char c = static_cast<unsigned char>(s[offset]);
        if (c < 0x80)
            offset += 1;
        else if ((c & 0xE0) == 0xC0)
            offset += 2;
        else if ((c & 0xF0) == 0xE0)
            offset += 3;
        else if ((c & 0xF8) == 0xF0)
            offset += 4;
        else
            offset += 1;
        chars++;
    }
    return chars;
}

std::vector<std::string> table_aligner::align_table_block(const std::vector<std::string>& lines, const align_options& opts) {
    if (lines.empty()) return {};

    std::vector<std::vector<std::string>> grid;
    std::vector<size_t> col_widths;
    
    for (const auto& line : lines) {
        if (is_header_separator(line)) {
            grid.push_back({"---SEPARATOR---"}); // Placeholder
            continue;
        }
        
        auto tokens = tokenize_row(line);
        grid.push_back(tokens);
        
        if (tokens.size() > col_widths.size()) {
            col_widths.resize(tokens.size(), 0);
        }
        
        for (size_t i = 0; i < tokens.size(); ++i) {
            col_widths[i] = std::max(col_widths[i], utf8_length(tokens[i]));
        }
    }
    
    std::vector<std::string> result;
    
    auto make_border = [&](const std::string& left, const std::string& mid, const std::string& right, const std::string& fill) {
        std::string line = left;
        for (size_t i = 0; i < col_widths.size(); ++i) {
            if (i > 0) line += mid;
            for (size_t j = 0; j < col_widths[i] + 2 * opts.padding; ++j) line += fill;
        }
        line += right;
        return line;
    };

    if (opts.use_utf8_frames) {
        result.push_back(make_border("┌", "┬", "┐", "─"));
    }

    for (size_t row_idx = 0; row_idx < grid.size(); ++row_idx) {
        const auto& row = grid[row_idx];
        std::string aligned_line;
        
        if (opts.use_utf8_frames) {
            if (row.size() == 1 && row[0] == "---SEPARATOR---") {
                aligned_line = make_border("├", "┼", "┤", "─");
            } else {
                aligned_line = "│";
                for (size_t i = 0; i < col_widths.size(); ++i) {
                    if (i > 0) aligned_line += "│";
                    aligned_line += std::string(opts.padding, ' ');
                    std::string cell = (i < row.size()) ? row[i] : "";
                    aligned_line += cell;
                    size_t cell_len = utf8_length(cell);
                    if (col_widths[i] > cell_len) {
                        aligned_line += std::string(col_widths[i] - cell_len, ' ');
                    }
                    aligned_line += std::string(opts.padding, ' ');
                }
                aligned_line += "│";
            }
        } else {
            if (opts.use_outer_pipes) aligned_line += "|";
            
            if (row.size() == 1 && row[0] == "---SEPARATOR---") {
                // Reconstruct separator based on widths
                for (size_t i = 0; i < col_widths.size(); ++i) {
                    if (i > 0) aligned_line += "|";
                    aligned_line += std::string(col_widths[i] + 2 * opts.padding, '-');
                }
            } else {
                for (size_t i = 0; i < col_widths.size(); ++i) {
                    if (i > 0) aligned_line += "|";
                    aligned_line += std::string(opts.padding, ' ');
                    
                    std::string cell = (i < row.size()) ? row[i] : "";
                    aligned_line += cell;
                    
                    size_t cell_len = utf8_length(cell);
                    if (col_widths[i] > cell_len) {
                        aligned_line += std::string(col_widths[i] - cell_len, ' ');
                    }
                    
                    aligned_line += std::string(opts.padding, ' ');
                }
            }
            if (opts.use_outer_pipes) aligned_line += "|";
        }
        result.push_back(aligned_line);
    }
    
    if (opts.use_utf8_frames) {
        result.push_back(make_border("└", "┴", "┘", "─"));
    }

    return result;
}

} // namespace markdown_utils
