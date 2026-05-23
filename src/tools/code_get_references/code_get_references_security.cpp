#include "code_get_references.h"

namespace tools {

code_get_references_tool::code_get_references_tool(code_get_references_args args) : args_(std::move(args)) {}

bool code_get_references_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    std::string safe_path;
    if (!ctx.fs_security.validate_access(args_.path, agentlib::access_type::read, safe_path, out_error)) {
        return false;
    }
    return true;
}

} // namespace tools
