#pragma once
#include <format>
#include <string>
#include "config_manager.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "tool_context.h"

namespace agentlib {

inline std::string update_file_health_state(tool_context &ctx, const std::string &safe_path) {
	ctx.edit_sequence_counter++;
	std::string edit_id = "#" + std::to_string(ctx.edit_sequence_counter);
	auto &health = ctx.file_health_tracker[safe_path];

	std::string build_dir = config_manager::get_instance().get_build_directory();
	std::string compile_cmd = fs_utils::get_compile_command_for_file(safe_path, build_dir, true);

	bool is_clean = true;
	bool has_health_data = false;

	if (!compile_cmd.empty()) {
		std::string full_cmd = "export LC_ALL=C.UTF-8 LANG=C.UTF-8 && " + compile_cmd;
		std::string output = fs_utils::execute_command_sync(full_cmd);
		has_health_data = true;
		is_clean = (output.find(": error:") == std::string::npos && output.find("fatal error:") == std::string::npos);
	} else {
		auto diags_opt = project_manager::get_instance().lsp_query_file_diagnostics(safe_path);
		if (diags_opt.has_value()) {
			has_health_data = true;
			is_clean = diags_opt->empty();
		}
	}

	if (!has_health_data) {
		health.state = lsp_health_state::unknown;
		health.originating_edit_id.clear();
		return edit_id;
	}

	if (is_clean) {
		health.state = lsp_health_state::clean;
		health.originating_edit_id.clear();
	} else {
		if (health.state == lsp_health_state::clean) {
			health.state = lsp_health_state::dirty;
			health.originating_edit_id = edit_id;
		} else if (health.state == lsp_health_state::unknown) {
			health.state = lsp_health_state::dirty;
			health.originating_edit_id.clear();
		} else {
			health.state = lsp_health_state::dirty;
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
		if (health.state == lsp_health_state::dirty && !health.originating_edit_id.empty()) {
			notes += std::format("\nℹ️ Diagnostic Note: File '{}' was clean and first developed errors after Edit {}.\n", path, health.originating_edit_id);
		}
	}
	return notes;
}

} // namespace agentlib
