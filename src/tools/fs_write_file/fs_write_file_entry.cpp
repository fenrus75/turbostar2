#include "fs_write_file.h"
#include <filesystem>
#include <fstream>

namespace tools {

fs_write_file_tool::fs_write_file_tool(fs_write_file_args args) : args_(std::move(args)) {}

bool fs_write_file_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    // 1. Open Document Check: Reject if the file is currently open in the editor.
    if (ctx.doc_provider) {
        auto doc_snapshot = ctx.doc_provider->get_open_document(args_.safe_path);
        if (doc_snapshot) {
            out_error = "Error: This file is currently open in the user's editor. You cannot overwrite an actively edited file. Please ask the user to close it.";
            return false;
        }
    }

    // 2. Existence Check
    if (std::filesystem::exists(args_.safe_path)) {
        if (!args_.force_overwrite) {
            out_error = "Error: File already exists. Set force_overwrite to true if you explicitly want to overwrite it.";
            return false;
        }
    }

    return true;
}

std::string fs_write_file_tool::execute(agentlib::tool_context& /*ctx*/) {
    try {
        std::ofstream out(args_.safe_path, std::ios::binary);
        if (!out.is_open()) {
            return "Error: Could not open file for writing.";
        }

        out << args_.content;
        out.close();

        return "Successfully wrote to " + args_.path;
    } catch (const std::exception& e) {
        return "Error writing to file: " + std::string(e.what());
    }
}

} // namespace tools
