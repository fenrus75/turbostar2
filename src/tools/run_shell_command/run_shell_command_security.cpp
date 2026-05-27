#include "../../agentlib/tool_registry.h"
#include "run_shell_command.h"

namespace tools
{

bool run_shell_command_validator::validate_args_impl(const nlohmann::json& args, const agentlib::tool_context& /*ctx*/, std::string& out_error) const
{
    parsed_args_.command = args["command"].get<std::string>();
    
    if (args.contains("timeout") && args["timeout"].is_number_integer()) {
        parsed_args_.timeout = args["timeout"].get<int>();
    } else {
        parsed_args_.timeout = 300;
    }

    if (args.contains("async") && args["async"].is_boolean()) {
        parsed_args_.is_async = args["async"].get<bool>();
    } else {
        parsed_args_.is_async = false;
    }

    if (parsed_args_.command.empty()) {
        out_error = "Command cannot be empty.";
        return false;
    }
    // Block ANSI escape sequences
    if (parsed_args_.command.find('\x1b') != std::string::npos) {
        out_error = "Command contains forbidden ANSI escape sequences.";
        return false;
    }
    return true;
}

std::unique_ptr<agentlib::llm_tool> run_shell_command_validator::create_tool_impl(const nlohmann::json& /*args*/) const
{
    return std::make_unique<run_shell_command_tool>(parsed_args_);
}

REGISTER_TOOL(run_shell_command_validator)

} // namespace tools
