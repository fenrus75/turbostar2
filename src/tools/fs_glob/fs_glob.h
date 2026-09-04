#pragma once
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "agentlib/llm_tool_action.h"
#include "agentlib/tool_validator.h"

namespace tools {

struct fs_glob_args {
    std::string pattern;
    std::string path{"."};
    std::string safe_search_path;
};

class fs_glob_tool : public agentlib::llm_tool_action {
public:
    explicit fs_glob_tool(fs_glob_args args);
    explicit fs_glob_tool(std::string pattern);

    bool validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::string execute(agentlib::tool_context& ctx) override;

private:
    fs_glob_args args_;
};

/*

# subclasses of fs_glob_validator

| subclass                | filename                    |
| ----------------------- | --------------------------- | 
| fs_find_files_validator | src/tools/fs_glob/fs_glob.h |

*/
class fs_glob_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_glob"; }
    std::string get_description() const override { 
        return "Returns a list of files matching a glob pattern (e.g. 'src/**/*.cpp', supporting double-star ** wildcards) relative to the project root. Can be scoped to a directory with 'path'."; 
    }
    nlohmann::json get_parameters_schema() const override;

    bool is_pure() const override { return true; }

protected:
    bool validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const override;
    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& args) const override;

    mutable fs_glob_args args_;
};

class fs_find_files_validator : public fs_glob_validator {
public:
    std::string get_name() const override { return "fs_find_files"; }
    std::string get_description() const override { 
        return "Alias for fs_glob: Find files matching a pattern or name within the workspace or a specified directory."; 
    }
};

} // namespace tools
