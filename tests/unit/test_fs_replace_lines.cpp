#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <nlohmann/json.hpp>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/tool_context.h"
#include "../../src/tools/fs_replace_lines/fs_replace_lines.h"

void create_dummy_file(const std::string& path) {
    std::ofstream out(path);
    out << "Line 1\nLine 2\nLine 3\n";
    out.close();
}

int main() {
    std::string test_file = "test_poem.txt";
    create_dummy_file(test_file);

    agentlib::tool_context ctx;
    ctx.fs_security.set_working_directory(std::filesystem::current_path());
    ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);
    ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);

    nlohmann::json args = {
        {"path", test_file},
        {"edits", nlohmann::json::array({
            {
                {"line_number", 2},
                {"type", "replace"},
                {"orgstring", "Line 2"},
                {"newstring", "Replaced Line 2"}
            }
        })}
    };

    auto& registry = agentlib::tool_registry::get_instance();
    std::string result = registry.execute_tool("fs_replace_lines", args.dump(), ctx);
    
    std::cout << "Tool Output: " << result << "\n";
    assert(result.find("Successfully applied") != std::string::npos);

    // Verify the file was changed
    std::ifstream in(test_file);
    std::string line;
    
    std::getline(in, line); assert(line == "Line 1");
    std::getline(in, line); assert(line == "Replaced Line 2");
    std::getline(in, line); assert(line == "Line 3");
    
    in.close();
    std::remove(test_file.c_str());

    std::cout << "fs_replace_lines unit test passed!\n";
    return 0;
}
