#pragma once
#include "file_security_manager.h"
#include "document_provider.h"
#include "../event_queue.h"

namespace agentlib {

class ai_agent; // Forward declaration

// A placeholder for the context that tools will receive.
// In a full integration, this would hold references to the active document,
// event queue, or other Turbostar editor state.
class tool_context {
public:
    file_security_manager fs_security;
    document_provider* doc_provider = nullptr;
    event_queue* queue = nullptr;
    ai_agent* active_agent = nullptr;
    // ... other contextual methods will be added here ...
};

} // namespace agentlib
