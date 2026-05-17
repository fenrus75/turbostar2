#include "fs_replace_lines.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tools {

fs_replace_lines_tool::fs_replace_lines_tool(fs_replace_args args) : args_(std::move(args)) {}

bool fs_replace_lines_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    // 1. Existence Check
    if (!std::filesystem::exists(args_.safe_path)) {
        out_error = "Error: File does not exist. fs_replace_lines can only edit existing files.";
        return false;
    }

    // 3. ATOMIC VERIFICATION: Verify ALL orgstrings before making any edits
    std::vector<std::string> lines;
    std::ifstream in(args_.safe_path);
    if (!in.is_open()) {
        out_error = "Error: Could not open file for reading during verification.";
        return false;
    }
    
    std::string line_content;
    while (std::getline(in, line_content)) {
        if (!line_content.empty() && line_content.back() == '\r') {
            line_content.pop_back();
        }
        lines.push_back(line_content);
    }
    in.close();

    for (const auto& edit : args_.edits) {
        if (edit.type == "add") continue;

        int idx = edit.line_number - 1;
        if (idx < 0 || idx >= static_cast<int>(lines.size())) {
            out_error = "Verification Error: line_number " + std::to_string(edit.line_number) + " is out of bounds.";
            return false;
        }

        std::string actual_content = lines[idx];
        std::string expected_prefix = edit.original_text;
        
        while (!expected_prefix.empty() && std::isspace(expected_prefix.back())) {
            expected_prefix.pop_back();
        }

        if (actual_content.find(expected_prefix) != 0) {
            out_error = "Verification Error at line " + std::to_string(edit.line_number) + 
                   ". \nExpected starting with: '" + expected_prefix + 
                   "'\nActual content: '" + actual_content + "'";
            return false;
        }
    }

    return true;
}

std::string fs_replace_lines_tool::execute(agentlib::tool_context& ctx) {
    if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
        // Create a JSON payload of the edits to send to the UI thread
        nlohmann::json edits_json = nlohmann::json::array();
        for (const auto& edit : args_.edits) {
            nlohmann::json edit_json;
            edit_json["line_number"] = edit.line_number;
            edit_json["type"] = edit.type;
            edit_json["original_text"] = edit.original_text;
            edit_json["replace_with"] = edit.replace_with;
            edits_json.push_back(edit_json);
        }
        
        ctx.doc_provider->apply_live_edits(args_.safe_path, edits_json.dump());
        return "Successfully dispatched " + std::to_string(args_.edits.size()) + " edits to the live editor buffer.";
    }
    return execute_disk_fallback();
}

std::string fs_replace_lines_tool::execute_disk_fallback() const {
    std::vector<std::string> lines;
    
    // 1. Read file into memory (we know it's valid from validate_runtime)
    std::ifstream in(args_.safe_path);
    if (!in.is_open()) {
        return "Error: Could not open file for reading during execution.";
    }
    
    std::string line_content;
    while (std::getline(in, line_content)) {
        if (!line_content.empty() && line_content.back() == '\r') {
            line_content.pop_back();
        }
        lines.push_back(line_content);
    }
    in.close();

    // 2. Apply edits (Guaranteed descending order and verified by validator)
    for (const auto& edit : args_.edits) {
        int idx = edit.line_number - 1;

        if (edit.type == "remove") {
            lines.erase(lines.begin() + idx);
        } else if (edit.type == "add") {
            // Split newstring by newlines to support multiline insertions
            std::vector<std::string> new_parts;
            if (edit.replace_with.empty()) {
                new_parts.push_back("");
            } else {
                std::stringstream ss(edit.replace_with);
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
            if (edit.replace_with.empty()) {
                new_parts.push_back("");
            } else {
                std::stringstream ss(edit.replace_with);
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
