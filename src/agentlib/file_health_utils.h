#pragma once
#include <format>
#include <string>
#include "../project_manager.h"
#include "tool_context.h"

namespace agentlib {

inline std::string update_file_health_state(tool_context &ctx, const std::string &safe_path) {
	ctx.edit_sequence_counter++;
	std::string edit_id = "#" + std::to_string(ctx.edit_sequence_counter);

	auto diags_opt = project_manager::get_instance().lsp_query_file_diagnostics(safe_path);
	auto &health = ctx.file_health_tracker[safe_path];

	if (!diags_opt.has_value()) {
		// LSP absent / offline / unsupported -> UNKNOWN
		health.state = lsp_health_state::unknown;
		health.originating_edit_id.clear();
		return edit_id;
	}

	// Check if there are any errors or warnings
	bool is_clean = diags_opt->empty();

	if (is_clean) {
		health.state = lsp_health_state::clean;
		health.originating_edit_id.clear();
	} else {
		if (health.state == lsp_health_state::clean) {
			// CLEAN -> DIRTY: Only transition where originating_edit_id is recorded!
			health.state = lsp_health_state::dirty;
			health.originating_edit_id = edit_id;
		} else if (health.state == lsp_health_state::unknown) {
			// UNKNOWN -> DIRTY: Do NOT set originating_edit_id
			health.state = lsp_health_state::dirty;
			health.originating_edit_id.clear();
		} else {
			// DIRTY -> DIRTY: Keep existing originating_edit_id
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
