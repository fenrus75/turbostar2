#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "wait_for_agent.h"

namespace tools
{

struct wait_for_agent_raw_args {
	int id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(wait_for_agent_raw_args, id);

class wait_for_agent_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "wait_for_agent";
	}
	std::string get_description() const override
	{
		return "Pauses your execution until the specified subagent finishes its current task and returns to an idle state.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to wait for."}}}}},
			{"required", nlohmann::json::array({"id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			wait_for_agent_raw_args raw_args = args_json.get<wait_for_agent_raw_args>();
			args_.id = raw_args.id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<wait_for_agent_tool>(args_);
	}

      private:
	mutable wait_for_agent_args args_;
};

REGISTER_TOOL(wait_for_agent_validator)

} // namespace tools