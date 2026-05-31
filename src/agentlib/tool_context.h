#pragma once
#include "file_security_manager.h"
#include "document_provider.h"
#include "../event_queue.h"
#include <functional>
#include <unordered_map>
#include <string>

namespace agentlib {

class ai_agent; // Forward declaration

struct file_drift_state {
    int cumulative_shift = 0;
    int edit_turns = 0;
};

// A placeholder for the context that tools will receive.
// In a full integration, this would hold references to the active document,
// event queue, or other Turbostar editor state.
class tool_context {
public:
    file_security_manager fs_security;
    document_provider* doc_provider = nullptr;
    event_queue* queue = nullptr;
    ai_agent* active_agent = nullptr;
    std::string tool_call_id;
    
    // Callback to trigger a UI redraw during long-running tool executions
    std::function<void()> trigger_ui_update;
    
    // File drift tracking state across a session
    std::unordered_map<std::string, file_drift_state> file_drift_tracker;
    // ... other contextual methods will be added here ...
};

} // namespace agentlib
