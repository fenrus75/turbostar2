#pragma once
#include <format>
#include <string>
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "tool_context.h"
#include "utf8.h"

namespace agentlib {

inline std::string update_file_health_state(tool_context &ctx, const std::string &safe_path) {
	ctx.edit_sequence_counter++;
	std::string edit_id = "#" + std::to_string(ctx.edit_sequence_counter);
	auto &health = ctx.file_health_tracker[safe_path];

	std::string build_dir = config_manager::get_instance().get_build_directory();
	std::string compile_cmd = fs_utils::get_compile_command_for_file(safe_path, build_dir, true);

	event_logger::get_instance().log("[file_health] update_file_health_state: path='{}', edit_id={}, state_before={}, compile_cmd='{}'",
					 safe_path, edit_id, static_cast<int>(health.state), compile_cmd);

	bool is_clean = true;
	bool has_health_data = false;

	if (!compile_cmd.empty()) {
		std::string full_cmd = "export LC_ALL=C.UTF-8 LANG=C.UTF-8 && " + compile_cmd;
		std::string output = fs_utils::execute_command_sync(full_cmd);
		std::string clean_output = utf8::sanitize_terminal_output(output);
		has_health_data = true;
		is_clean = (clean_output.find(": error:") == std::string::npos &&
			    clean_output.find("fatal error:") == std::string::npos &&
			    clean_output.find("Process exited with code 0") != std::string::npos);
		event_logger::get_instance().log("[file_health] single-file compile check: is_clean={}, output_snippet='{}'",
						 is_clean, clean_output.substr(0, std::min<size_t>(clean_output.length(), 200)));
	} else {
		auto diags_opt = project_manager::get_instance().lsp_query_file_diagnostics(safe_path);
		if (diags_opt.has_value()) {
			has_health_data = true;
			is_clean = diags_opt->empty();
			event_logger::get_instance().log("[file_health] LSP diagnostics check: is_clean={}, diag_count={}",
							 is_clean, diags_opt->size());
		} else {
			event_logger::get_instance().log("[file_health] no compile_cmd and no LSP diagnostics available for '{}'", safe_path);
		}
	}

	if (!has_health_data) {
		health.state = lsp_health_state::unknown;
		health.originating_edit_id.clear();
		event_logger::get_instance().log("[file_health] state updated to UNKNOWN for '{}'", safe_path);
		return edit_id;
	}

	if (is_clean) {
		health.state = lsp_health_state::clean;
		health.originating_edit_id.clear();
		event_logger::get_instance().log("[file_health] state updated to CLEAN for '{}'", safe_path);
	} else {
		if (health.state == lsp_health_state::clean) {
			health.state = lsp_health_state::dirty;
			health.originating_edit_id = edit_id;
			event_logger::get_instance().log("[file_health] state transitioned CLEAN -> DIRTY for '{}', originating_edit_id={}", safe_path, edit_id);
		} else if (health.state == lsp_health_state::unknown) {
			health.state = lsp_health_state::dirty;
			health.originating_edit_id.clear();
			event_logger::get_instance().log("[file_health] state transitioned UNKNOWN -> DIRTY for '{}' (no originating_edit_id)", safe_path);
		} else {
			health.state = lsp_health_state::dirty;
			event_logger::get_instance().log("[file_health] state remained DIRTY for '{}', originating_edit_id={}", safe_path, health.originating_edit_id);
		}
	}

	return edit_id;
}

inline std::string get_file_health_attribution_note(const tool_context &ctx, const std::string &safe_path) {
	auto it = ctx.file_health_tracker.find(safe_path);
	if (it != ctx.file_health_tracker.end() && it->second.state == lsp_health_state::dirty && !it->second.originating_edit_id.empty()) {
		return std::format("\nℹ️ Diagnostic Note: File '{}' was clean and first developed errors after Edit {}.\n", safe_path, it->second.originating_edit_id);
	}
	return "";
}

inline std::string get_all_file_health_attribution_notes(const tool_context &ctx) {
	std::string notes;
	for (const auto &[path, health] : ctx.file_health_tracker) {
		event_logger::get_instance().log("[file_health] attribution check: path='{}', state={}, originating_edit_id='{}'",
						 path, static_cast<int>(health.state), health.originating_edit_id);
		if (health.state == lsp_health_state::dirty && !health.originating_edit_id.empty()) {
			notes += std::format("\nℹ️ Diagnostic Note: File '{}' was clean and first developed errors after Edit {}.\n", path, health.originating_edit_id);
		}
	}
	return notes;
}

} // namespace agentlib
