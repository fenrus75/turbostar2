#include "agent_get_run_screenshot.h"
#include <nlohmann/json.hpp>

namespace tools
{

bool agent_get_run_screenshot_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_get_run_screenshot_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	agentlib::run_screenshot_data snap = ctx.doc_provider->get_run_screenshot(args_.run_id);
	if (snap.grid.empty()) {
		set_failure(ctx, "Run ID " + std::to_string(args_.run_id) + " not found or empty screen.");
		return "Error: Run not found.";
	}

	nlohmann::json snap_json = {
	    {"grid", snap.grid},
	    {"cursor_x", snap.cursor_x},
	    {"cursor_y", snap.cursor_y},
	    {"cursor_visible", snap.cursor_visible}};

	set_success(ctx, "Captured screenshot of run_id " + std::to_string(args_.run_id));
	return snap_json.dump();
}

} // namespace tools
