#include <memory>
#include <nlohmann/json.hpp>
#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_create.h"

namespace tools
{

struct agent_create_raw_args {
	std::string name;
	std::string profile;
	std::string task;
	bool wait{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_create_raw_args, name, profile, task, wait);

class agent_create_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return false;
	} // Modifies agent state

	std::string get_name() const override
	{
		return "create_agent";
	}
	std::string get_description() const override
	{
		return "Creates a new subagent to delegate tasks to. You must provide either a 'task' (user request) or a 'profile' "
		       "(system instructions), or both.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "A short, descriptive name for the subagent."}}},
		      {"profile",
		       {{"type", "string"},
			{"description", "System instructions and personality profile for the subagent. Optional if 'task' is "
					"provided."}}},
		      {"task",
		       {{"type", "string"},
			{"description", "The initial task or request for the subagent to perform. Optional if 'profile' is "
					"provided."}}},
		      {"wait",
		       {{"type", "boolean"},
			{"description", "If true, the tool will wait for the subagent to complete its task and will return its "
					"final response directly. Defaults to false (asynchronous).",
			 "default", false}}}}},
		    {"required", nlohmann::json::array({"name"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_create_raw_args raw_args = args_json.get<agent_create_raw_args>();
			if (raw_args.name.empty()) {
				out_error = "Agent name cannot be empty.";
				return false;
			}
			if (raw_args.profile.empty() && raw_args.task.empty()) {
				out_error = "You must provide either a 'profile' or a 'task' to create an agent.";
				return false;
			}
			args_.name = raw_args.name;
			args_.profile = raw_args.profile;
			args_.task = raw_args.task;
			args_.wait = raw_args.wait;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_create_tool>(args_);
	}

      private:
	mutable agent_create_args args_;
};

REGISTER_TOOL(agent_create_validator)

} // namespace tools