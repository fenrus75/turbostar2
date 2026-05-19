#include "fs_read_binary.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include <nlohmann/json.hpp>
#include <optional>

namespace tools {

struct fs_read_binary_raw_args {
    std::string path;
    int start_offset = 0;
    int size = -1;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(fs_read_binary_raw_args, path, start_offset, size);

class fs_read_binary_validator : public agentlib::tool_validator {
public:
    std::string get_name() const override { return "fs_read_binary"; }
    std::string get_description() const override { return "Reads binary content from a file and returns it as a base64 encoded string. Can read a specific range using start_offset and size."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"path", {
                    {"type", "string"},
                    {"description", "The path to the file, relative to the project root."}
                }},
                {"start_offset", {
                    {"type", "integer"},
                    {"description", "The 0-based byte offset to start reading from. Defaults to 0."}
                }},
                {"size", {
                    {"type", "integer"},
                    {"description", "The number of bytes to read. Defaults to reading the rest of the file if omitted. A maximum limit (e.g., 50MB) may apply."}
                }}
            }},
            {"required", nlohmann::json::array({"path"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& raw_json, const agentlib::tool_context& ctx, std::string& out_error) const override {
        try {
            fs_read_binary_raw_args parsed = raw_json.get<fs_read_binary_raw_args>();
            
            if (parsed.path.empty()) {
                out_error = "Path parameter cannot be empty.";
                return false;
            }

            std::string canonical_path;
            if (!ctx.fs_security.validate_access(parsed.path, agentlib::access_type::read, canonical_path, out_error)) {
                return false;
            }

            args_.requested_path = parsed.path;
            args_.safe_path = canonical_path;
            args_.start_offset = (parsed.start_offset < 0) ? 0 : parsed.start_offset;
            args_.size = parsed.size;

            return true;
        } catch (const std::exception& e) {
            out_error = "Invalid arguments: " + std::string(e.what());
            return false;
        }
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*raw_json*/) const override {
        return std::make_unique<fs_read_binary_tool>(args_);
    }

private:
    mutable fs_read_binary_args args_;
};

REGISTER_TOOL(fs_read_binary_validator)

} // namespace tools