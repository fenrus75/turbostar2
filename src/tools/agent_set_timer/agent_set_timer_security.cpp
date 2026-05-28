#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_set_timer.h"

namespace tools
{

struct agent_set_timer_raw_args {
	int seconds;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_set_timer_raw_args, seconds);

class agent_set_timer_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_set_timer";
	}
	std::string get_description() const override
	{
		return "Schedules a background timer. When the timer expires, if you are idle, a notification will wake you up.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"seconds", {{"type", "integer"}, {"description", "The duration in seconds to wait."}}}}},
			{"required", nlohmann::json::array({"seconds"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_set_timer_raw_args raw_args = args_json.get<agent_set_timer_raw_args>();
			args_.seconds = raw_args.seconds;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_set_timer_tool>(args_);
	}

      private:
	mutable agent_set_timer_args args_;
};

REGISTER_TOOL(agent_set_timer_validator)

} // namespace tools
