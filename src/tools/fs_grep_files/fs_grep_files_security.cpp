#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "fs_grep_files.h"

namespace tools {

nlohmann::json fs_grep_files_validator::get_parameters_schema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "The exact string or RE2 regular expression to search for."}
            }},
            {"is_regex", {
                {"type", "boolean"},
                {"description", "Set to true if the pattern is a regular expression. Defaults to false for literal string search."},
                {"default", false}
            }},
            {"include_ext", {
                {"type", "string"},
                {"description", "Filter by file extension (e.g., '.cpp', '.py'). Optional."}
            }},
            {"search_path", {
                {"type", "string"},
                {"description", "Restrict search to a specific file or directory path relative to project root. Defaults to the document root if omitted."}
            }},
            {"max_results", {
                {"type", "integer"},
                {"description", "Cap the total number of detailed matches (line content + number) to prevent blowing out the context window. Defaults to 50."},
                {"default", 50}
            }},
            {"context_lines", {
                {"type", "integer"},
                {"description", "Number of lines of context to include before and after the match. Defaults to 0 (only the matching line). Max is 10."},
                {"default", 0}
            }}
        }},
        {"required", nlohmann::json::array({"pattern"})}
    };
}

bool fs_grep_files_validator::validate_args_impl(const nlohmann::json& raw_args, const agentlib::tool_context& ctx, std::string& out_error) const {
    try {
        args_.pattern = raw_args.value("pattern", "");
        args_.is_regex = raw_args.value("is_regex", false);
        if (raw_args.contains("include_ext") && raw_args["include_ext"].is_string()) {
            args_.include_ext = raw_args["include_ext"].get<std::string>();
        } else {
            args_.include_ext = std::nullopt;
        }
        
        if (raw_args.contains("search_path") && raw_args["search_path"].is_string()) {
            args_.search_path = raw_args["search_path"].get<std::string>();
        } else {
            args_.search_path = std::nullopt;
        }
        
        args_.max_results = raw_args.value("max_results", 50);
        args_.context_lines = raw_args.value("context_lines", 0);
        if (args_.context_lines < 0) args_.context_lines = 0;
        if (args_.context_lines > 10) args_.context_lines = 10;

        if (args_.pattern.empty()) {
            out_error = "Pattern cannot be empty.";
            return false;
        }

        std::string search_path = ""; // Default to root
        if (args_.search_path) {
            search_path = *args_.search_path;
        }

        // Use validate_access with read permission, even for directories, to ensure it's within bounds
        std::string canonical_path;
        if (!ctx.fs_security.validate_access(search_path, agentlib::access_type::read, canonical_path, out_error)) {
            // It's possible validate_access doesn't like directories if they don't exist yet, but for search they must exist.
            return false;
        }
        
        args_.safe_search_path = canonical_path;

        return true;

    } catch (const std::exception& e) {
        out_error = "Invalid arguments: " + std::string(e.what());
        return false;
    }
}

std::unique_ptr<agentlib::llm_tool> fs_grep_files_validator::create_tool_impl(const nlohmann::json& /*args*/) const {
    return std::make_unique<fs_grep_files_tool>(args_);
}

REGISTER_TOOL(fs_grep_files_validator)

} // namespace tools