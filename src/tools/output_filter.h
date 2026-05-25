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
    virtual void apply(std::vector<std::string>& lines) const = 0;
};

// Test filter that removes lines containing "FILTER TEST DELETE ME"
class test_delete_me_filter : public output_filter {
public:
    bool is_applicable(const std::string& /*command*/, const std::string& raw_output) const override {
        return raw_output.find("FILTER TEST DELETE ME") != std::string::npos;
    }

    void apply(std::vector<std::string>& lines) const override {
        lines.erase(std::remove_if(lines.begin(), lines.end(), [](const std::string& line) {
            return line.find("FILTER TEST DELETE ME") != std::string::npos;
        }), lines.end());
    }
};

inline std::string apply_output_filters(const std::string& command, const std::string& raw_output, const std::vector<std::shared_ptr<output_filter>>& filters) {
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
    for (const auto& filter : applicable_filters) {
        filter->apply(lines);
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