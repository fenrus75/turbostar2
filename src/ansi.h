#pragma once
#include <string_view>

namespace ansi
{

// Enable mouse tracking (DEC 1002) and bracketed paste
void enable_terminal_modes();

// Disable mouse tracking and bracketed paste
void disable_terminal_modes();

// Safe reset for crash recovery (disable mouse, disable bracketed paste, show cursor, reset color)
void reset_terminal_state();

// OSC 52 Copy to Clipboard
void copy_to_clipboard(std::string_view text);

} // namespace ansi
