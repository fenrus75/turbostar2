#include "../../agentlib/single_string_tool_validator.h"
#include "../../agentlib/tool_registry.h"
#include "agent_complete_todo.h"

namespace tools
{

class agent_complete_todo_validator : public agentlib::single_string_tool_validator
{
      public:
	std::string get_name() const override
	{
		return "agent_complete_todo";
	}
	std::string get_description() const override
	{
		return "Marks a task as complete in the AI agent's internal todo list. Provide an exact string match or a unique "
		       "substring.";
	}
	std::string get_parameter_name() const override
	{
		return "text";
	}
	std::string get_parameter_description() const override
	{
		return "The exact task text or a unique substring to match.";
	}

	bool validate_string_arg(const std::string & /*arg*/, const agentlib::tool_context & /*ctx*/,
				 std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_from_string(const std::string &arg) const override
	{
		return std::make_unique<agent_complete_todo_tool>(arg);
	}
};

REGISTER_TOOL(agent_complete_todo_validator)

} // namespace tools