#include "fs_replace_lines.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tools {

fs_replace_lines_tool::fs_replace_lines_tool(fs_replace_args args) : args_(std::move(args)) {}

bool fs_replace_lines_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    // 1. Open Document Check
    if (ctx.doc_provider) {
        auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
        if (doc_snapshot) {
            // For this phase, we reject if the file is open.
            // Phase 2 will implement live document modification.
            out_error = "Error: This file is currently open in the user's editor. Live UI modification is not yet implemented. Please ask the user to close the file.";
            return false;
        }
    }

    // 2. Existence Check
    if (!std::filesystem::exists(args_.safe_path)) {
        out_error = "Error: File does not exist. fs_replace_lines can only edit existing files.";
        return false;
    }

    return true;
}

std::string fs_replace_lines_tool::execute(agentlib::tool_context& /*ctx*/) {
    return execute_disk_fallback();
}

std::string fs_replace_lines_tool::execute_disk_fallback() const {
    std::vector<std::string> lines;
    
    // 1. Read file into memory
    std::ifstream in(args_.safe_path);
    if (!in.is_open()) {
        return "Error: Could not open file for reading.";
    }
    
    std::string line_content;
    while (std::getline(in, line_content)) {
        if (!line_content.empty() && line_content.back() == '\r') {
            line_content.pop_back();
        }
        lines.push_back(line_content);
    }
    in.close();

    // 2. Verify orgstrings
    for (const auto& edit : args_.edits) {
        if (edit.type == "add") continue;

        int idx = edit.line_number - 1;
        if (idx < 0 || idx >= static_cast<int>(lines.size())) {
            return "Verification Error: line_number " + std::to_string(edit.line_number) + " is out of bounds.";
        }

        std::string actual_content = lines[idx];
        std::string expected_prefix = edit.orgstring;
        
        // Strip trailing whitespace from expected_prefix for loose matching
        while (!expected_prefix.empty() && std::isspace(expected_prefix.back())) {
            expected_prefix.pop_back();
        }

        if (actual_content.find(expected_prefix) != 0) {
            return "Verification Error at line " + std::to_string(edit.line_number) + 
                   ". \nExpected starting with: '" + expected_prefix + 
                   "'\nActual content: '" + actual_content + "'";
        }
    }

    // 3. Apply edits (Guaranteed descending order by validator)
    for (const auto& edit : args_.edits) {
        int idx = edit.line_number - 1;

        if (edit.type == "remove") {
            lines.erase(lines.begin() + idx);
        } else if (edit.type == "add") {
            // Split newstring by newlines to support multiline insertions
            std::vector<std::string> new_parts;
            if (edit.newstring.empty()) {
                new_parts.push_back("");
            } else {
                std::stringstream ss(edit.newstring);
                std::string part;
                while (std::getline(ss, part)) {
                    if (!part.empty() && part.back() == '\r') part.pop_back();
                    new_parts.push_back(part);
                }
            }
            lines.insert(lines.begin() + idx, new_parts.begin(), new_parts.end());

        } else if (edit.type == "replace") {
            // Replace the current line with the new lines
            lines.erase(lines.begin() + idx);
            
            std::vector<std::string> new_parts;
            if (edit.newstring.empty()) {
                new_parts.push_back("");
            } else {
                std::stringstream ss(edit.newstring);
                std::string part;
                while (std::getline(ss, part)) {
                    if (!part.empty() && part.back() == '\r') part.pop_back();
                    new_parts.push_back(part);
                }
                // std::getline strips the trailing newline. If the original string ended in a newline,
                // getline won't yield an empty string for it. But for single lines without a newline,
                // this works perfectly.
            }

            lines.insert(lines.begin() + idx, new_parts.begin(), new_parts.end());
        }
    }

    // 4. Write back to disk
    std::ofstream out(args_.safe_path, std::ios::binary);
    if (!out.is_open()) {
        return "Error: Could not open file for writing.";
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        out << lines[i];
        if (i < lines.size() - 1 || true) { // Always end with newline
            out << "\n";
        }
    }
    out.close();

    return "Successfully applied " + std::to_string(args_.edits.size()) + " edits to " + args_.path;
}

} // namespace tools
