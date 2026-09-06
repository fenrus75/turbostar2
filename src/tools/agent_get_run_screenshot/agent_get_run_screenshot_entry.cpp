#include "tools/agent_get_run_screenshot/agent_get_run_screenshot.h"
#include "fs_utils.h"
#include <chrono>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
		std::string err_msg = "Error: Run not found.";
		if (!snap.is_alive) {
			err_msg = "Error: Run ID not found or application process already ended.";
		}
		if (!snap.crash_notification.empty()) {
			err_msg = std::format("{}\n{}", err_msg, snap.crash_notification);
		}
		set_failure(ctx, std::format("Run ID {} not found or empty screen.", args_.run_id));
		return err_msg;
	}

	std::vector<std::string> clean_grid;
	clean_grid.reserve(snap.grid.size());
	for (const auto &line : snap.grid) {
		std::string clean_line;
		clean_line.reserve(line.size());
		for (char c : line) {
			if (static_cast<unsigned char>(c) >= 32 && c != 127) {
				clean_line += c;
			} else if (c == '\t') {
				clean_line += ' ';
			}
		}
		// Trim trailing whitespace from each line
		size_t last_non_space = clean_line.find_last_not_of(" \t");
		if (last_non_space != std::string::npos) {
			clean_line.erase(last_non_space + 1);
		} else {
			clean_line.clear();
		}
		clean_grid.push_back(std::move(clean_line));
	}

	// Remove trailing empty lines from the bottom of the grid
	while (!clean_grid.empty() && clean_grid.back().empty()) {
		clean_grid.pop_back();
	}

	std::string formatted_output;
	if (!snap.crash_notification.empty()) {
		formatted_output += std::format("{}\n", snap.crash_notification);
	}

	std::string_view cursor_info = snap.cursor_visible ? "" : "; cursor hidden";
	std::string_view status_str = snap.is_alive ? "process running" : "process ended";

	formatted_output += std::format("[Terminal Screen (cursor at row {}, col {}{}; {})]:\n```\n",
					snap.cursor_y, snap.cursor_x, cursor_info, status_str);

	for (const auto &line : clean_grid) {
		formatted_output += line;
		formatted_output += '\n';
	}
	formatted_output += "```\n";

	set_success(ctx, std::format("Captured terminal dump of run_id {}", args_.run_id));
	std::string result_tag = std::format("{}_result", args_.tool_name);
	return fs_utils::wrap_prompt_untrusted_data_tag(result_tag, formatted_output);
}

} // namespace tools
