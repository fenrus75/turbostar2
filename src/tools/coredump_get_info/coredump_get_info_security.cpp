#include "coredump_get_info.h"
#include "../../agentlib/tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools {

struct coredump_get_info_raw_args {
    int pid;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(coredump_get_info_raw_args, pid);

class coredump_get_info_validator : public agentlib::tool_validator {
public:
    bool is_pure() const override { return true; } // Only reading data

    std::string get_name() const override { return "coredump_get_info"; }
    std::string get_description() const override { return "Retrieves the detailed backtrace and GDB analysis of a specific coredump by its PID."; }
    
    nlohmann::json get_parameters_schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pid", {{"type", "integer"}, {"description", "The Process ID (PID) of the crashed executable."}}}
            }},
            {"required", nlohmann::json::array({"pid"})}
        };
    }

protected:
    bool validate_args_impl(const nlohmann::json& args_json, const agentlib::tool_context& /*ctx*/, std::string& out_error) const override {
        try {
            coredump_get_info_raw_args raw_args = args_json.get<coredump_get_info_raw_args>();
            args_.pid = raw_args.pid;
            return true;
        } catch (const std::exception& e) {
            out_error = "Argument parsing error: " + std::string(e.what());
            return false;
        }
    }

    std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json& /*args*/) const override {
        return std::make_unique<coredump_get_info_tool>(args_);
    }

private:
    mutable coredump_get_info_args args_;
};

REGISTER_TOOL(coredump_get_info_validator)

}