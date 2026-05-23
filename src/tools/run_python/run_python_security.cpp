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
    std::string get_description() const override { 
        std::string base_desc = "Executes Python code. You MUST use print() statements to see output, as the script runs headlessly. Provide either 'code' (for direct execution) OR 'file_path' (to run an existing file).";
        if (has_uv()) {
            return base_desc + " Optionally provide an array of PyPI 'dependencies' to be temporarily installed via 'uv'.";
        }
        return base_desc;
    }
    
    nlohmann::json get_parameters_schema() const override {
        nlohmann::json props = {
            {"code", {{"type", "string"}, {"description", "The raw Python code string to execute."}}},
            {"file_path", {{"type", "string"}, {"description", "The relative path to a Python script to execute."}}}
        };

        if (has_uv()) {
            props["dependencies"] = {
                {"type", "array"}, 
                {"items", {{"type", "string"}}}, 
                {"description", "A list of PyPI packages required by the script (e.g., ['requests', 'beautifulsoup4'])."}
            };
        }

        return {
            {"type", "object"},
            {"properties", props}
        };
    }

private:
    bool has_uv() const {
        static bool uv_checked = false;
        static bool uv_available = false;
        if (!uv_checked) {
            uv_available = (system("which uv > /dev/null 2>&1") == 0);
            uv_checked = true;
        }
        return uv_available;
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