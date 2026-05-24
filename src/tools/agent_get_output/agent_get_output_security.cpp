#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_get_output.h"

namespace tools
{

struct agent_get_output_raw_args {
	int id;
	bool keep{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_output_raw_args, id, keep);

class agent_get_output_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Changed to false because it might delete the agent

	std::string get_name() const override
	{
		return "agent_get_output";
	}
	std::string get_description() const override
	{
		return "Retrieves the interaction history of a subagent. By default, it also terminates the agent unless 'keep' is "
		       "set to true.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to query."}}},
			  {"keep",
			   {{"type", "boolean"},
			    {"description", "If true, the subagent is kept alive after its output is retrieved. Defaults to false "
					    "(auto-terminate)."},
			    {"default", false}}}}},
			{"required", nlohmann::json::array({"id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_get_output_raw_args raw_args = args_json.get<agent_get_output_raw_args>();
			args_.id = raw_args.id;
			args_.keep = raw_args.keep;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_get_output_tool>(args_);
	}

      private:
	mutable agent_get_output_args args_;
};

REGISTER_TOOL(agent_get_output_validator)

} // namespace tools