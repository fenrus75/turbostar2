#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "get_current_datetime.h"

namespace tools
{

class get_current_datetime_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "get_current_datetime";
	}
	std::string get_description() const override
	{
		return "Returns the current date and time as a markdown table. Includes Unix time, Year, Month, Day, Hour, Minute, Second, "
		       "and Timezone.";
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
		return std::make_unique<get_current_datetime_tool>();
	}
};

REGISTER_TOOL(get_current_datetime_validator)

} // namespace tools