#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/tool_registry.h"
#include "agentlib/tool_validator.h"
#include "tools/agent_get_run_screenshot/agent_get_run_screenshot.h"

namespace tools
{

/**
 * @brief Raw argument structure for JSON deserialization.
 */
struct agent_get_run_screenshot_raw_args {
	int run_id{-1};
	bool settle{false};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(agent_get_run_screenshot_raw_args, run_id, settle);

nlohmann::json agent_get_run_terminaldump_validator::get_parameters_schema() const
{
	return {
	    {"type", "object"},
	    {"properties",
	     {{"run_id", {{"type", "integer"}, {"description", "The unique execution ID returned by run_executable."}}},
	      {"settle", {{"type", "boolean"}, {"description", "Optional. If true, waits up to 3 seconds for the screen content to settle (no changes for 250 ms) before dumping the terminal screen."}}}}},
	    {"required", nlohmann::json::array({"run_id"})}};
}

bool agent_get_run_terminaldump_validator::validate_args_impl(const nlohmann::json &untrusted_args, const agentlib::tool_context & /*ctx*/,
							      std::string &out_error) const
{
	if (!untrusted_args.contains("run_id")) {
		out_error = "Argument parsing error: missing required field 'run_id'";
		return false;
	}
	try {
		agent_get_run_screenshot_raw_args raw = untrusted_args.get<agent_get_run_screenshot_raw_args>();
		if (raw.run_id < 0) {
			out_error = "Invalid run_id specified: must be non-negative.";
			return false;
		}
		args_ = agent_get_run_screenshot_args{raw.run_id, raw.settle, get_name()};
		return true;
	} catch (const std::exception &e) {
		out_error = std::format("Argument parsing error: {}", e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> agent_get_run_terminaldump_validator::create_tool_impl(const nlohmann::json & /*args*/) const
{
	args_.tool_name = get_name();
	return std::make_unique<agent_get_run_screenshot_tool>(args_);
}

REGISTER_TOOL(agent_get_run_terminaldump_validator)
REGISTER_TOOL(agent_get_run_screenshot_validator)
REGISTER_TOOL(agent_run_get_terminaldump_validator)
REGISTER_TOOL(agent_run_get_screenshot_validator)

} // namespace tools

