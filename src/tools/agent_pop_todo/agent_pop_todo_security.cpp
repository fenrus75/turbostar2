#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_pop_todo.h"

namespace tools
{

class agent_pop_todo_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "pop_todo";
	}
	std::string get_description() const override
	{
		return "Removes and returns the first item from the agent's todo list. Useful for treating the todo list as a "
		       "sequential task queue.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"}, {"properties", nlohmann::json::object()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json & /*raw_json*/, const agentlib::tool_context & /*ctx*/,
				std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*raw_json*/) const override
	{
		return std::make_unique<agent_pop_todo_tool>();
	}
};

REGISTER_TOOL(agent_pop_todo_validator)

} // namespace tools
