#pragma once

namespace crash_handler
{

// Installs the fallback crash signal handlers if no custom handler is already registered.
void install_fallback_handler();

} // namespace crash_handler
