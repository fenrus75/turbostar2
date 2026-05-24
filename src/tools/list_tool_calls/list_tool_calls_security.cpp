#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "list_tool_calls.h"

namespace tools
{

class list_tool_calls_validator : public agentlib::tool_validator
{
      public:
	std::string get_name() const override
	{
		return "list_tool_calls";
	}
	std::string get_description() const override
	{
		return "Lists all available LLM tools and their descriptions as a Markdown table. Use this to introspect your "
		       "capabilities.";
	}
	bool is_pure() const override
	{
		return true;
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
		return std::make_unique<list_tool_calls_tool>();
	}
};

REGISTER_TOOL(list_tool_calls_validator)

} // namespace tools