#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_list_episodes.h"

namespace tools
{

class agent_list_episodes_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_list_episodes";
	}
	std::string get_description() const override
	{
		return "Lists all archived/paged-out episodes, returning a markdown table showing the episode ID and their 'when to resume' reactivation hint.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"}, {"properties", nlohmann::json::object()}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json & /*args*/, const agentlib::tool_context & /*ctx*/,
				std::string & /*out_error*/) const override
	{
		return true;
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_list_episodes_tool>();
	}
};

REGISTER_TOOL(agent_list_episodes_validator)

} // namespace tools
