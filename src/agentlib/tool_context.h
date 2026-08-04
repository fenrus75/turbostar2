#pragma once
#include <functional>
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
};

} // namespace agentlib
