#include "../../agentlib/tool_registry.h"
#include "../../agentlib/tool_validator.h"
#include "agent_get_profile_details.h"
#include <memory>
#include <nlohmann/json.hpp>

namespace tools
{

struct agent_get_profile_details_raw_args {
	std::string file_path;
	std::string function_name;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_profile_details_raw_args, file_path, function_name);

class agent_get_profile_details_validator : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	}

	std::string get_name() const override
	{
		return "agent_get_profile_details";
	}
	std::string get_description() const override
	{
		return "Returns line-by-line CPU cycle percentages for a target source file or function name from the most recent performance profile run.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"file_path",
		       {{"type", "string"},
			{"description", "Source file path to filter line performance samples. Optional."}}},
		      {"function_name",
		       {{"type", "string"},
			{"description", "Function name to filter line performance samples. Optional."}}}}}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		try {
			agent_get_profile_details_raw_args raw = args_json.get<agent_get_profile_details_raw_args>();
			args_.file_path = raw.file_path;
			args_.function_name = raw.function_name;
			return true;
		} catch (const std::exception &e) {
			out_error = "Argument parsing error: " + std::string(e.what());
			return false;
		}
	}

	std::unique_ptr<agentlib::llm_tool> create_tool_impl(const nlohmann::json & /*args*/) const override
	{
		return std::make_unique<agent_get_profile_details_tool>(args_);
	}

      private:
	mutable agent_get_profile_details_args args_;
};

REGISTER_TOOL(agent_get_profile_details_validator)

} // namespace tools
