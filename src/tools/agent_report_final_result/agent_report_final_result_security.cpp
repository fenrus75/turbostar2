#include "agentlib/single_string_tool_validator.h"
#include "agentlib/tool_registry.h"
#include "agent_report_final_result.h"

namespace tools
{

class agent_report_final_result_validator : public agentlib::single_string_tool_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_report_final_result";
	}
	std::string get_description() const override
	{
		return "Reports the final result of the subagent's execution back to the parent agent. Use this when the task is completed.";
	}
	std::string get_parameter_name() const override
	{
		return "result";
	}
	std::string get_parameter_description() const override
	{
		return "The final result or summary of the completed task.";
	}

	bool validate_string_arg(const std::string &arg, const agentlib::tool_context & /*ctx*/,
	                         std::string &out_error) const override
	{
		for (unsigned char c : arg) {
			if (c < 32 && c != 9 && c != 10 && c != 13) {
				out_error = "Security Violation: Final result contains unsafe control characters.";
				return false;
			}
		}
		return true;
	}
	std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string &arg) const override
	{
		return std::make_unique<agent_report_final_result_tool>(arg);
	}
};

REGISTER_TOOL(agent_report_final_result_validator)

} // namespace tools
