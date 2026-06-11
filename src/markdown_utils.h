#pragma once

#include <string>
#include <vector>

namespace markdown_utils {

struct align_options {
    int padding = 1;              // Spaces between text and the | separator
    bool use_outer_pipes = true;  // Ensure table starts and ends with |
    bool use_utf8_frames = false; // Use UTF-8 box drawing characters
};

struct table_range {
    size_t start_line; // 0-based index
    size_t end_line;   // 0-based index (inclusive)
};

class table_aligner {
public:
    /**
     * @brief Find ranges of lines that constitute markdown tables.
     */
    static std::vector<table_range> find_table_ranges(const std::vector<std::string>& lines);

    /**
     * @brief Takes a vector of lines representing ONE table and returns the aligned version.
     */
    static std::vector<std::string> align_table_block(const std::vector<std::string>& lines, const align_options& opts = {});

    /**
     * @brief Heuristic to detect if a line is part of a markdown table.
     */
    static bool is_table_row(const std::string& line);

    /**
     * @brief Heuristic to detect the "--- | ---" header separator line.
     */
    static bool is_header_separator(const std::string& line);

private:
    static std::vector<std::string> tokenize_row(const std::string& line);
    static std::string trim(const std::string& s);
};

/**
 * @brief Get the visual display width of a UTF-8 string on a terminal.
 */
size_t display_width(const std::string& s);

/**
 * @brief Detects and aligns all markdown tables in a block of text.
 */
std::string align_all_tables(const std::string& text, bool framed = false);

} // namespace markdown_utils

