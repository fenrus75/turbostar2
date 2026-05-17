#include "fs_file_size.h"
#include <filesystem>

namespace tools {

fs_file_size_tool::fs_file_size_tool(std::string safe_path) : safe_path_(std::move(safe_path)) {}

bool fs_file_size_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string fs_file_size_tool::execute(agentlib::tool_context& /*ctx*/) {
    try {
        std::error_code ec;
        auto size = std::filesystem::file_size(safe_path_, ec);
        if (ec) {
            return "Error reading file size: " + ec.message();
        }
        return std::to_string(size) + " bytes";
    } catch (const std::exception& e) {
        return "Error: " + std::string(e.what());
    }
}

} // namespace tools
