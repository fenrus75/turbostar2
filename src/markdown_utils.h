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

    /**
     * @brief Get the number of UTF-8 characters in a string.
     */
    static size_t utf8_length(const std::string& s);

private:
    static std::vector<std::string> tokenize_row(const std::string& line);
    static std::string trim(const std::string& s);
};

} // namespace markdown_utils
