#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_get_profile_summary.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools
{

struct agent_get_profile_summary_raw_args {
	int limit{10};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_profile_summary_raw_args, limit);

class agent_get_profile_summary_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_get_profile_summary";
	}
	std::string get_description() const override
	{
		return "Returns the top functions and lines by CPU cycle percentage from the most recent performance profile run.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"limit",
		       {{"type", "integer"},
			{"description", "Maximum number of top functions and lines to return. Defaults to 10."},
			{"default", 10}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_get_profile_summary_raw_args raw = args_json.get<agent_get_profile_summary_raw_args>();
			if (raw.limit <= 0) {
				out_error = "Validation Error: limit must be greater than 0.";
				return false;
			}
			args_.limit = raw.limit;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_get_profile_summary_tool>(args_);
	}

      private:
	mutable agent_get_profile_summary_args args_;
};

REGISTER_TOOL(agent_get_profile_summary_validator)

} // namespace tools
