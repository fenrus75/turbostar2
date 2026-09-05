#include "agentlib/tool_registry.h"
#include "fs_glob.h"

namespace tools {

nlohmann::json fs_glob_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "The glob pattern to search for (e.g. '*.cpp', 'src/**/*.cpp', or '*turbomcp*'). Alias: 'query'."}
            }},
            {"query", {
                {"type", "string"},
                {"description", "Alias for 'pattern': The glob pattern to search for."}
            }},
            {"path", {
                {"type", "string"},
                {"description", "Optional. The root directory to search within, relative to the project root or VFS URI (e.g. 'src', 'build', or 'include://'). Defaults to project root ('.'). Aliases: 'search_path', 'directory'."},
                {"default", "."}
            }},
            {"search_path", {
                {"type", "string"},
                {"description", "Alias for 'path': The root directory to search within. Defaults to project root ('.')."}
            }},
            {"directory", {
                {"type", "string"},
                {"description", "Alias for 'path': The root directory to search within. Defaults to project root ('.')."}
            }}
        }}
    };
}

bool fs_glob_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& ctx, std::string& out_error) const {
    std::string untrusted_path = ".";
    if (args.contains("path") && args["path"].is_string()) {
        untrusted_path = args["path"].get<std::string>();
    } else if (args.contains("search_path") && args["search_path"].is_string()) {
        untrusted_path = args["search_path"].get<std::string>();
    } else if (args.contains("directory") && args["directory"].is_string()) {
        untrusted_path = args["directory"].get<std::string>();
    }

    if (untrusted_path.empty()) {
        untrusted_path = ".";
    }

    if (untrusted_path.find("..") != std::string::npos) {
        out_error = "Directory path cannot contain '..' directory traversal.";
        return false;
    }

    std::string untrusted_pattern;
    if (args.contains("pattern") && args["pattern"].is_string()) {
        untrusted_pattern = args["pattern"].get<std::string>();
    } else if (args.contains("query") && args["query"].is_string()) {
        untrusted_pattern = args["query"].get<std::string>();
    } else {
        // Hybrid default: if an explicit subdirectory is targeted (not root), default pattern to "*"
        if (untrusted_path != "." && untrusted_path != "./") {
            untrusted_pattern = "*";
        } else {
            out_error = "Missing 'pattern' parameter. If you want to list the contents of a directory, use fs_list_dir(path='...'). To search files recursively across the project, specify a pattern (e.g. pattern='*.cpp').";
            return false;
        }
    }

    if (untrusted_pattern.empty()) {
        out_error = "Parameter 'pattern' cannot be empty.";
        return false;
    }

    if (untrusted_pattern.find("..") != std::string::npos) {
        out_error = "Glob pattern cannot contain '..' directory traversal.";
        return false;
    }

    args_.pattern = untrusted_pattern;
    args_.path = untrusted_path;
    args_.safe_search_path = "";

    if (untrusted_path.find("://") == std::string::npos) {
        std::string safe_path;
        if (!ctx.fs_security.validate_access(untrusted_path, agentlib::access_type::read, safe_path, out_error)) {
            return false;
        }
        args_.safe_search_path = safe_path;
    } else {
        args_.safe_search_path = untrusted_path;
    }

    return true;
}

std::unique_ptr<agentlib::llm_tool> fs_glob_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
    return std::make_unique<fs_glob_tool>(args_);
}

REGISTER_TOOL(fs_glob_validator)
REGISTER_TOOL(fs_find_files_validator)

} // namespace tools
