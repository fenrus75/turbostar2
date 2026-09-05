#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agent_get_profile_summary.h"

namespace tools
{

struct agent_get_profile_summary_raw_args {
	int limit{10};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_profile_summary_raw_args, limit);

class agent_get_profile_summary_validator : public agentlib::tool_validator
{
      public:
	// Pure Domain 2 (Agent & Workflow State): Reads in-memory profile summary data.
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
		return "Returns top functions and source lines ranked by CPU cycle percentage from the active or specified performance profile run.";;
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id",
		       {{"description",
			 "Optional execution run ID returned by run_executable (e.g., '1', '2', or 'editor'). Omit for latest profile."},
			{"oneOf", nlohmann::json::array({nlohmann::json{{"type", "string"}}, nlohmann::json{{"type", "integer"}}})}}},
		      {"limit",
		       {{"type", "integer"},
			{"description", "Maximum number of top functions and lines to return. Defaults to 10 (max 100)."},
			{"default", 10}}},
		      {"format",
		       {{"type", "string"},
			{"enum", nlohmann::json::array({"markdown", "json"})},
			{"description", "Return format. 'markdown' for clean token-efficient table, 'json' for raw JSON. Defaults to 'markdown'."},
			{"default", "markdown"}}}}}};
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
			args_.limit = 10;
			if (args_json.contains("limit") && args_json["limit"].is_number()) {
				int lim = args_json["limit"].get<int>();
				if (lim <= 0 || lim > 100) {
					out_error = "Validation Error: limit must be between 1 and 100.";
					return false;
				}
				args_.limit = lim;
			}
			args_.format = "markdown";
			if (args_json.contains("format") && args_json["format"].is_string()) {
				std::string fmt = args_json["format"].get<std::string>();
				if (fmt != "markdown" && fmt != "json") {
					out_error = "Validation Error: format must be 'markdown' or 'json'.";
					return false;
				}
				args_.format = fmt;
			}
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
