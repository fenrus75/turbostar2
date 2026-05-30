#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agent_get_output.h"

namespace tools
{

/**
 * @brief Raw argument structure for JSON deserialization.
 */
struct agent_get_output_raw_args {
	int id{-1};
	bool keep{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_output_raw_args, id, keep);

/**
 * @brief Validator for the agent_get_output tool.
 */
class agent_get_output_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Not pure because it can terminate/remove subagents.

	std::string get_name() const override
	{
		return "agent_get_output";
	}
	std::string get_description() const override
	{
		return "Retrieves the interaction history of a subagent. By default, it also terminates the agent unless 'keep' is set to true.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties",
			 {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to query."}}},
			  {"keep",
			   {{"type", "boolean"},
			    {"description", "If true, the subagent is kept alive after its output is retrieved. Defaults to false (auto-terminate)."},
			    {"default", false}}}}},
			{"required", nlohmann::json::array({"id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		if (!args_json.contains("id")) {
			out_error = "Argument parsing error: missing required field 'id'";
			return false;
		}
		try {
			agent_get_output_raw_args raw_args = args_json.get<agent_get_output_raw_args>();
			if (raw_args.id < 0) {
				out_error = "Invalid subagent ID (must be non-negative)";
				return false;
			}
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json &args) const override
	{
		agent_get_output_raw_args raw_args = args.get<agent_get_output_raw_args>();
		return std::make_unique<agent_get_output_tool>(agent_get_output_args{raw_args.id, raw_args.keep});
	}
};

REGISTER_TOOL(agent_get_output_validator)

} // namespace tools