#include "agent_get_run_screenshot.h"
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <chrono>
#include <thread>

namespace tools
{

/**
 * @brief Validates the runtime context and arguments.
 */
bool agent_get_run_screenshot_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Error: Document provider unavailable";
		return false;
	}
	if (args_.run_id < 0) {
		out_error = "Error: run_id must be non-negative";
		return false;
	}
	return true;
}

/**
 * @brief Executes the retrieval of the terminal screenshot.
 */
std::string agent_get_run_screenshot_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Error: Document provider unavailable");
		return "Error: Document provider unavailable";
	}
	if (args_.run_id < 0) {
		set_failure(ctx, "Error: run_id must be non-negative");
		return "Error: run_id must be non-negative";
	}

	if (args_.settle) {
		auto start_time = std::chrono::steady_clock::now();
		while (true) {
			int64_t age = ctx.doc_provider->get_run_last_modified_age(args_.run_id);
			if (age >= 250) {
				break;
			}
			auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
			if (elapsed_ms >= 3000) {
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	agentlib::run_screenshot_data snap = ctx.doc_provider->get_run_screenshot(args_.run_id);
	if (snap.grid.empty()) {
		set_failure(ctx, std::format("Run ID {} not found or empty screen.", args_.run_id));
		return "Error: Run not found.";
	}

	try {
		nlohmann::json snap_json = {
		    {"grid", snap.grid},
		    {"cursor_x", snap.cursor_x},
		    {"cursor_y", snap.cursor_y},
		    {"cursor_visible", snap.cursor_visible}};

		set_success(ctx, std::format("Captured screenshot of run_id {}", args_.run_id));
		return snap_json.dump();
	} catch (const std::exception &e) {
		set_failure(ctx, std::format("JSON serialization failed: {}", e.what()));
		return "Error: JSON serialization failed.";
	}
}

} // namespace tools
