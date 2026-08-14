#pragma once

namespace crash_handler
{

// [NOT Signal-Safe]
// Installs the fallback crash signal handlers if no custom handler is already registered.
// Must be called once during application initialization.
void install_fallback_handler();

// [Signal-Safe Handle]
// Open file descriptor for the active crash log, or -1 if deferred until crash.
extern int crash_fd;

} // namespace crash_handler
