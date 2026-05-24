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
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_create_raw_args, name, profile);

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
		return "Creates a new subagent to delegate tasks to. The profile defines the agent's initial prompt and instructions.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"name", {{"type", "string"}, {"description", "A short, descriptive name for the subagent."}}},
		      {"profile",
		       {{"type", "string"}, {"description", "The initial prompt and system instructions for the subagent to execute."}}}}},
		    {"required", nlohmann::json::array({"name", "profile"})}};
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
			if (raw_args.profile.empty()) {
				out_error = "Agent profile cannot be empty.";
				return false;
			}
			args_.name = raw_args.name;
			args_.profile = raw_args.profile;
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