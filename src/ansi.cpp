#include "ansi.h"
#include "fs_utils.h"
#include <unistd.h>
#include <string.h>

namespace ansi
{

void enable_terminal_modes()
{
	// DEC 1002 mouse click/drag tracking, bracketed paste mode on
	const char* seq = "\033[?1002h\033[?2004h\n";
	write(STDOUT_FILENO, seq, strlen(seq));
}

void disable_terminal_modes()
{
	// DEC 1002 mouse click/drag tracking off, bracketed paste mode off
	const char* seq = "\033[?1002l\033[?2004l\n";
	write(STDOUT_FILENO, seq, strlen(seq));
}

void reset_terminal_state()
{
	// Disable mouse/paste modes, show cursor, reset text color attributes
	const char* seq = "\033[?1002l\033[?2004l\033[?25h\033[0m\n";
	write(STDOUT_FILENO, seq, strlen(seq));
}

void copy_to_clipboard(std::string_view text)
{
	std::string encoded = fs_utils::base64_encode(text);
	// Format: ESC ] 52 ; c ; <base64> BEL
	std::string seq = "\033]52;c;" + encoded + "\a";
	write(STDOUT_FILENO, seq.data(), seq.size());
}

} // namespace ansi
