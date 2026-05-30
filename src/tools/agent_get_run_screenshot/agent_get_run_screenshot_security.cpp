#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "agent_get_run_screenshot.h"

namespace tools
{

/**
 * @brief Raw argument structure for JSON deserialization.
 */
struct agent_get_run_screenshot_raw_args {
	int run_id{-1};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_run_screenshot_raw_args, run_id);

/**
 * @brief Validator for the agent_get_run_screenshot tool.
 */
class agent_get_run_screenshot_validator final : public agentlib::tool_validator
{
      public:
	bool is_pure() const override
	{
		return true;
	} // Reads screen buffer snapshot without side effects.

	std::string get_name() const override
	{
		return "agent_get_run_screenshot";
	}
	std::string get_description() const override
	{
		return "Returns a snapshot/screenshot of the terminal buffer grid, cursor coordinates, and visibility status for a given run ID.";
	}

	nlohmann::json get_parameters_schema() const override
	{
		return {
		    {"type", "object"},
		    {"properties",
		     {{"run_id", {{"type", "integer"}, {"description", "The unique execution ID returned by agent_start_app."}}}}},
		    {"required", nlohmann::json::array({"run_id"})}};
	}

      protected:
	bool validate_args_impl(const nlohmann::json &args_json, const agentlib::tool_context & /*ctx*/,
				std::string &out_error) const override
	{
		if (!args_json.contains("run_id")) {
			out_error = "Argument parsing error: missing required field 'run_id'";
			return false;
		}
		try {
			agent_get_run_screenshot_raw_args raw = args_json.get<agent_get_run_screenshot_raw_args>();
			if (raw.run_id < 0) {
				out_error = "Invalid run_id specified.";
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
		agent_get_run_screenshot_raw_args raw = args.get<agent_get_run_screenshot_raw_args>();
		return std::make_unique<agent_get_run_screenshot_tool>(agent_get_run_screenshot_args{raw.run_id});
	}
};

REGISTER_TOOL(agent_get_run_screenshot_validator)

} // namespace tools
