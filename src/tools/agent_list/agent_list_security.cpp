#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agent_list.h"

namespace tools
{

/**
 * @brief Validator for the agent_list tool.
 */
class agent_list_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	} // Simple listing of subagents, does not mutate state.

	std::string get_name() const override
	{
		return "list_agents";
	}
	std::string get_description() const override
	{
		return "Lists all active subagents managed by the current agent, returning a markdown table of ID, name, and status.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", nlohmann::json::object()},
			{"additionalProperties", false}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json & /*args*/, const agentlib::tool_context & /*ctx*/,
				std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_list_tool>();
	}
};

REGISTER_TOOL(agent_list_validator)

} // namespace tools