#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include "agentlib/agent_run_registry.h"
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
	return {{"type", "object"},
		{"properties",
		 {{"run_id",
		   {{"type", nlohmann::json::array({"integer", "string"})},
		    {"description", "Optional execution run ID (e.g. 1010, '1010', 'run_1010') or alias: 'gdb' (latest debugger session), "
				    "'app' (latest application run), or 'latest'. If omitted, automatically defaults to the active "
				    "debugger session (if present) or the most recent run."}}},
		  {"settle",
		   {{"type", "boolean"},
		    {"description", "Optional. If true, waits up to 3 seconds for the screen content to settle (no changes for 250 ms) "
				    "before dumping the terminal screen."}}}}},
		{"required", nlohmann::json::array()}};
}

bool agent_get_run_terminaldump_validator::validate_args_impl(const nlohmann::json &untrusted_args, const agentlib::tool_context &ctx,
							      std::string &out_error) const
{
	try {
		int resolved_run_id = -1;
		if (ctx.run_registry) {
			if (!ctx.run_registry->resolve_from_json(untrusted_args, agentlib::run_target_kind::debugger, resolved_run_id,
								 out_error)) {
				return false;
			}
		} else {
			if (!untrusted_args.contains("run_id") || !untrusted_args["run_id"].is_number_integer()) {
				out_error = "Argument parsing error: missing required field 'run_id'";
				return false;
			}
			resolved_run_id = untrusted_args["run_id"].get<int>();
			if (resolved_run_id < 0) {
				out_error = "Invalid run_id specified: must be non-negative.";
				return false;
			}
		}

		bool settle = false;
		if (untrusted_args.contains("settle") && untrusted_args["settle"].is_boolean()) {
			settle = untrusted_args["settle"].get<bool>();
		}
		args_ = agent_get_run_screenshot_args{resolved_run_id, settle, get_name()};
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
