#pragma once
#include <deque>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include "../event_queue.h"
#include "document_provider.h"
#include "file_security_manager.h"
#include "agent_properties.h"

namespace agentlib
{

class ai_agent; // Forward declaration

struct file_drift_state {
	int cumulative_shift = 0;
	int edit_turns = 0;
};

enum class lsp_health_state {
	unknown,
	clean,
	dirty
};

struct file_health_state {
	lsp_health_state state = lsp_health_state::unknown;
	std::string originating_edit_id;
};

struct codemap_history_entry {
	std::filesystem::file_time_type last_mtime{};
	std::set<std::string> reported_symbol_names;
};

// A placeholder for the context that tools will receive.
// In a full integration, this would hold references to the active document,
// event queue, or other Turbostar editor state.
class tool_context
{
      public:
	file_security_manager fs_security;
	document_provider *doc_provider = nullptr;
	event_queue *queue = nullptr;
	ai_agent *active_agent = nullptr;
	agent_properties properties;
	bool mutation_possible = true;
	std::string tool_call_id;

	// Callback to trigger a UI redraw during long-running tool executions
	std::function<void()> trigger_ui_update;

	// Callback to check if a tool family is active
	std::function<bool(const std::string &)> is_family_active;

	// File drift tracking state across a session
	std::unordered_map<std::string, file_drift_state> file_drift_tracker;

	// File health tracking state machine (CLEAN / DIRTY / UNKNOWN) across a session
	std::unordered_map<std::string, file_health_state> file_health_tracker;
	size_t edit_sequence_counter = 0;

	// Recent grep search patterns for codemap relevance boosting
	std::deque<std::string> recent_grep_patterns;

	// Per-file codemap history tracking for deduplication
	std::unordered_map<std::string, codemap_history_entry> codemap_history;

	// One-time session hint flag for fs_file_codemap tool availability in truncated codemaps
	bool has_hinted_fs_file_codemap = false;
};

} // namespace agentlib
