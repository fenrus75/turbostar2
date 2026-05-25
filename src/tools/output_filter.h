#pragma once
#include <vector>
#include <string>
#include <memory>
#include <sstream>
#include <algorithm>

namespace tools {

class output_filter {
public:
    virtual ~output_filter() = default;

    // Fast check to see if this filter is applicable to the output/command
    virtual bool is_applicable(const std::string& command, const std::string& raw_output) const = 0;

    // Process the vector of lines in-place, removing or summarizing lines as needed
    // Returns the number of lines removed.
    virtual int apply(std::vector<std::string>& lines) const = 0;
};

// Filter that compresses successful Meson/Ninja compilation steps
class meson_compile_filter : public output_filter {
public:
    bool is_applicable(const std::string& command, const std::string& /*raw_output*/) const override {
        return command.find("meson compile") != std::string::npos || command.find("ninja") != std::string::npos;
    }

    int apply(std::vector<std::string>& lines) const override {
        if (lines.empty()) return 0;

        auto is_progress_line = [](const std::string& line) {
            if (line.length() < 5 || line[0] != '[') return false;
            size_t end_bracket = line.find(']');
            if (end_bracket == std::string::npos) return false;
            size_t slash = line.find('/');
            if (slash == std::string::npos || slash > end_bracket) return false;
            
            // Verify everything between brackets are digits and a slash
            for (size_t i = 1; i < slash; ++i) {
                if (!std::isdigit(line[i])) return false;
            }
            for (size_t i = slash + 1; i < end_bracket; ++i) {
                if (!std::isdigit(line[i])) return false;
            }
            return true;
        };

        std::vector<std::string> new_lines;
        int removed_count = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            bool current_is_progress = is_progress_line(lines[i]);
            bool next_is_progress = (i + 1 < lines.size()) ? is_progress_line(lines[i+1]) : false;

            // If this line is a progress line AND the next line is ALSO a progress line,
            // it means this compile step succeeded silently with no warnings/errors. We drop it.
            if (current_is_progress && next_is_progress) {
                removed_count++;
                continue;
            }
            new_lines.push_back(lines[i]);
        }
        lines = std::move(new_lines);
        return removed_count;
    }
};

// Filter that compresses successful Meson test steps
class meson_test_filter : public output_filter {
public:
    bool is_applicable(const std::string& command, const std::string& /*raw_output*/) const override {
        return command.find("meson test") != std::string::npos;
    }

    int apply(std::vector<std::string>& lines) const override {
        if (lines.empty()) return 0;

        auto is_success_line = [](const std::string& line) {
            // E.g.: " 1/66 unit_event_logger                OK              0.01s"
            if (line.find(" OK ") == std::string::npos) return false;
            
            size_t start = 0;
            while (start < line.length() && std::isspace(line[start])) start++;
            if (start >= line.length() || !std::isdigit(line[start])) return false;
            
            size_t slash = line.find('/', start);
            if (slash == std::string::npos) return false;
            
            return true;
        };

        std::vector<std::string> new_lines;
        int removed_count = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            bool current_is_success = is_success_line(lines[i]);
            bool next_is_success = (i + 1 < lines.size()) ? is_success_line(lines[i+1]) : false;

            if (current_is_success && next_is_success) {
                removed_count++;
                continue;
            }
            new_lines.push_back(lines[i]);
        }
        lines = std::move(new_lines);
        return removed_count;
    }
};

inline std::string apply_output_filters(const std::string& command, const std::string& raw_output, const std::vector<std::shared_ptr<output_filter>>& filters, int* out_removed_count = nullptr) {
    std::vector<std::shared_ptr<output_filter>> applicable_filters;
    for (const auto& filter : filters) {
        if (filter->is_applicable(command, raw_output)) {
            applicable_filters.push_back(filter);
        }
    }
    
    if (applicable_filters.empty()) {
        return raw_output;
    }
    
    // Split into lines
    std::vector<std::string> lines;
    std::stringstream ss(raw_output);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    
    // Check if original string ended in a newline so we can preserve it
    bool ends_with_newline = !raw_output.empty() && raw_output.back() == '\n';
    
    // Apply filters
    int total_removed = 0;
    for (const auto& filter : applicable_filters) {
        total_removed += filter->apply(lines);
    }
    
    if (out_removed_count) {
        *out_removed_count = total_removed;
    }
    
    // Rejoin
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i < lines.size() - 1 || ends_with_newline) {
            result += "\n";
        }
    }
    return result;
}

} // namespace tools