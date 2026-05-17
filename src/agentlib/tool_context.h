#pragma once
#include "file_security_manager.h"

namespace agentlib {

// A placeholder for the context that tools will receive.
// In a full integration, this would hold references to the active document,
// event queue, or other Turbostar editor state.
class tool_context {
public:
    file_security_manager fs_security;
    // ... other contextual methods will be added here ...
};

} // namespace agentlib
