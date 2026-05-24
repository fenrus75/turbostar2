#include "../../agentlib/tool_registry.h"
#include "run_shell_command.h"

namespace tools
{

bool run_shell_command_validator::validate_string_arg(const std::string &arg, const agentlib::tool_context & /*ctx*/,
						      std::string &out_error) const
{
	if (arg.empty()) {
		out_error = "Command cannot be empty.";
		return false;
	}
	// Block ANSI escape sequences
	if (arg.find('\x1b') != std::string::npos) {
		out_error = "Command contains forbidden ANSI escape sequences.";
		return false;
	}
	return true;
}

std::unique_ptr<agentlib::llm_tool> run_shell_command_validator::create_tool_from_string(const std::string &arg) const
{
	return std::make_unique<run_shell_command_tool>(arg);
}

REGISTER_TOOL(run_shell_command_validator)

} // namespace tools