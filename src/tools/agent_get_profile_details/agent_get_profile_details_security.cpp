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
		return "Returns line-by-line CPU cycle percentages for a target source file or function name from a performance profile run (defaults to latest profile).";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id",
		       {{"description",
			 "Optional execution run ID returned by agent_start_app (e.g., '1', '2', or 'editor'). Omit for latest profile."},
			{"oneOf", nlohmann::json::array({nlohmann::json{{"type", "string"}}, nlohmann::json{{"type", "integer"}}})}}},
		      {"file_path",
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
			args_.run_id.clear();
			if (args_json.contains("run_id")) {
				if (args_json["run_id"].is_string()) {
					args_.run_id = args_json["run_id"].get<std::string>();
				} else if (args_json["run_id"].is_number()) {
					args_.run_id = std::to_string(args_json["run_id"].get<int>());
				}
			}
			args_.file_path.clear();
			if (args_json.contains("file_path") && args_json["file_path"].is_string()) {
				args_.file_path = args_json["file_path"].get<std::string>();
			}
			args_.function_name.clear();
			if (args_json.contains("function_name") && args_json["function_name"].is_string()) {
				args_.function_name = args_json["function_name"].get<std::string>();
			}
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
