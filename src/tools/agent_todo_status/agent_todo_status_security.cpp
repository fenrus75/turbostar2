#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_todo_status.h"

namespace tools
{

struct agent_todo_status_raw_args {
	int id;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_todo_status_raw_args, id);

class agent_todo_status_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_todo_status";
	}
	std::string get_description() const override
	{
		return "Returns the todo list with completion status of a specific subagent by its ID.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {{"type", "object"},
			{"properties", {{"id", {{"type", "integer"}, {"description", "The ID of the subagent to query."}}}}},
			{"required", nlohmann::json::array({"id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_todo_status_raw_args raw_args = args_json.get<agent_todo_status_raw_args>();
			args_.id = raw_args.id;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_todo_status_tool>(args_);
	}

      private:
	mutable agent_todo_status_args args_;
};

REGISTER_TOOL(agent_todo_status_validator)

} // namespace tools