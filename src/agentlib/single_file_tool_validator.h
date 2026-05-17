#pragma once
#include <string>
#include <memory>
#include "single_string_tool_validator.h"

namespace agentlib {

// A specialized base class for tools that operate on a single file path.
// It automatically intercepts the LLM's path argument, canonicalizes it,
// and validates it against the tool_context's file_security_manager.
class single_file_tool_validator : public single_string_tool_validator {
public:
    virtual ~single_file_tool_validator() = default;

    // Derived classes specify what level of access they need
    virtual access_type get_required_permission() const = 0;

    // The derived class must implement this to create the actual tool,
    // receiving the fully resolved and authorized absolute path.
    virtual std::unique_ptr<llm_tool> create_tool_from_resolved_path(const std::string& safe_path) const = 0;

protected:
    // Override the string hook to perform the security manager check
    bool validate_string_arg(const std::string& arg, const tool_context& ctx, std::string& out_error) const final {
        // Because validators are instantiated per-execution, we can safely mutate state here.
        // We use mutable to allow storing the resolved path while maintaining const-correctness 
        // for the public interface if needed, or just cast it. 
        // Actually, since create_tool is also const, we need a mutable field.
        if (!ctx.fs_security.validate_access(arg, get_required_permission(), resolved_path_, out_error)) {
            return false;
        }
        return true;
    }

    // Pass the SAFE, RESOLVED path to the tool creation hook
    std::unique_ptr<llm_tool> create_tool_from_string(const std::string& /*arg*/) const final {
        return create_tool_from_resolved_path(resolved_path_);
    }

private:
    mutable std::string resolved_path_;
};

} // namespace agentlib
