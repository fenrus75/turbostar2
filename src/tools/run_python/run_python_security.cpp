#include "run_python.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools {

struct run_python_raw_args {
    std::optional<std::string> code;
    std::optional<std::string> file_path;
    std::optional<std::vector<std::string>> dependencies;
};

void from_json(const nlohmann::json& j, run_python_raw_args& p) {
    if (j.contains("code")) p.code = j.at("code").get<std::string>();
    if (j.contains("file_path")) p.file_path = j.at("file_path").get<std::string>();
    if (j.contains("dependencies")) p.dependencies = j.at("dependencies").get<std::vector<std::string>>();
}

class run_python_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return false; }

    std::string get_name() const override { return "run_python"; }
    std::string get_description() const override { return "Executes Python code. Provide either 'code' (for direct execution) OR 'file_path' (to run an existing file). Optionally provide an array of PyPI 'dependencies' to be temporarily installed via 'uv'. Note: If 'uv' is not available on the host, dependencies may be ignored."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"code", {{"type", "string"}, {"description", "The raw Python code string to execute."}}},
                {"file_path", {{"type", "string"}, {"description", "The relative path to a Python script to execute."}}},
                {"dependencies", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "A list of PyPI packages required by the script (e.g., ['requests', 'beautifulsoup4'])."}}}
            }}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        try {
            run_python_raw_args raw_args = args_json.get<run_python_raw_args>();
            
            if (!raw_args.code.has_value() && !raw_args.file_path.has_value()) {
                out_error = "Must provide exactly one of 'code' or 'file_path'.";
                return false;
            }
            if (raw_args.code.has_value() && raw_args.file_path.has_value()) {
                out_error = "Cannot provide both 'code' and 'file_path'. Choose one.";
                return false;
            }

            args_.code = raw_args.code;
            args_.file_path = raw_args.file_path;
            if (raw_args.dependencies.has_value()) {
                args_.dependencies = raw_args.dependencies.value();
            }
            return true;
        } catch (const std::exception& e) {
            out_error = "Argument parsing error: " + std::string(e.what());
            return false;
        }
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<run_python_tool>(args_);
    }

private:
    mutable run_python_args args_;
};

REGISTER_TOOL(run_python_validator)

}