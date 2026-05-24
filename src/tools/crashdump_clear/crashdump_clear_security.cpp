#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "crashdump_clear.h"

namespace tools
{

class crashdump_clear_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	}

	std::string get_name() const override
	{
		return "crashdump_clear";
	}
	std::string get_description() const override
	{
		return "Deletes all crash dumps from the disk and clears the internal crash dump list. Use this to remove stale "
		       "crash dumps after they have been investigated.";
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
		return std::make_unique<crashdump_clear_tool>();
	}
};

REGISTER_TOOL(crashdump_clear_validator)

} // namespace tools
