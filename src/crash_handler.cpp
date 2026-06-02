#define UNW_LOCAL_ONLY
#include "crash_handler.h"
#include <libunwind.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

namespace crash_handler
{

static size_t safe_strlen(const char *s)
{
	size_t len = 0;
	while (s && s[len]) {
		len++;
	}
	return len;
}

static void safe_itoa(long val, char *buf, int buf_size)
{
	if (buf_size < 2)
		return;
	if (val == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}

	char temp[32];
	int i = 0;
	bool neg = false;
	if (val < 0) {
		neg = true;
		val = -val;
	}

	while (val > 0 && i < 31) {
		temp[i++] = (val % 10) + '0';
		val /= 10;
	}

	if (neg && i < 31) {
		temp[i++] = '-';
	}

	int j = 0;
	while (i > 0 && j < buf_size - 1) {
		buf[j++] = temp[--i];
	}
	buf[j] = '\0';
}

static void safe_hex_toa(unsigned long val, char *buf, int buf_size)
{
	if (buf_size < 2)
		return;
	if (val == 0) {
		buf[0] = '0';
		buf[1] = '\0';
		return;
	}

	char temp[32];
	int i = 0;
	const char *hex_chars = "0123456789abcdef";

	while (val > 0 && i < 31) {
		temp[i++] = hex_chars[val % 16];
		val /= 16;
	}

	int j = 0;
	while (i > 0 && j < buf_size - 1) {
		buf[j++] = temp[--i];
	}
	buf[j] = '\0';
}

static void fallback_signal_handler(int sig, siginfo_t *info, void *ucontext)
{
	(void)info;

	// Reset terminal to sane state (disable mouse/bracketed paste, show cursor)
	const char *reset_seq = "\033[?1002l\033[?2004l\033[?25h\033[0m\n";
	write(STDOUT_FILENO, reset_seq, safe_strlen(reset_seq));

	const char *msg_prefix = "\n*** Turbostar Fallback Crash Catcher ***\nCaught signal: ";
	write(STDOUT_FILENO, msg_prefix, safe_strlen(msg_prefix));

	char sig_buf[16];
	safe_itoa(sig, sig_buf, sizeof(sig_buf));
	write(STDOUT_FILENO, sig_buf, safe_strlen(sig_buf));

	const char *sig_name = " (Unknown)";
	if (sig == SIGSEGV) {
		sig_name = " (SIGSEGV - Segmentation Fault)";
	} else if (sig == SIGABRT) {
		sig_name = " (SIGABRT - Aborted)";
	} else if (sig == SIGFPE) {
		sig_name = " (SIGFPE - Floating Point Exception)";
	} else if (sig == SIGILL) {
		sig_name = " (SIGILL - Illegal Instruction)";
	} else if (sig == SIGBUS) {
		sig_name = " (SIGBUS - Bus Error)";
	}
	write(STDOUT_FILENO, sig_name, safe_strlen(sig_name));
	write(STDOUT_FILENO, "\n\nStack trace:\n", 15);

	unw_cursor_t cursor;
	if (unw_init_local(&cursor, reinterpret_cast<unw_context_t *>(ucontext)) == 0) {
		int frame = 0;
		do {
			unw_word_t ip;
			if (unw_get_reg(&cursor, UNW_REG_IP, &ip) == 0) {
				write(STDOUT_FILENO, "  #", 3);
				char frame_num_str[16];
				safe_itoa(frame, frame_num_str, sizeof(frame_num_str));
				write(STDOUT_FILENO, frame_num_str, safe_strlen(frame_num_str));

				write(STDOUT_FILENO, " 0x", 3);
				char ip_str[32];
				safe_hex_toa(ip, ip_str, sizeof(ip_str));
				write(STDOUT_FILENO, ip_str, safe_strlen(ip_str));

				char symbol[256];
				unw_word_t offset;
				if (unw_get_proc_name(&cursor, symbol, sizeof(symbol), &offset) == 0) {
					write(STDOUT_FILENO, " in ", 4);
					write(STDOUT_FILENO, symbol, safe_strlen(symbol));
					write(STDOUT_FILENO, " + 0x", 5);
					char offset_str[32];
					safe_hex_toa(offset, offset_str, sizeof(offset_str));
					write(STDOUT_FILENO, offset_str, safe_strlen(offset_str));
				}
				write(STDOUT_FILENO, "\n", 1);
			}
			frame++;
		} while (unw_step(&cursor) > 0 && frame < 128);
	} else {
		const char *err_msg = "  Failed to initialize stack unwinding via libunwind.\n";
		write(STDOUT_FILENO, err_msg, safe_strlen(err_msg));
	}

	// Restore default handler and re-raise signal to cleanly terminate process
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
	_exit(128 + sig);
}

void install_fallback_handler()
{
	// Query current handler for SIGSEGV to check for a pre-existing custom handler
	struct sigaction old_sa;
	if (sigaction(SIGSEGV, nullptr, &old_sa) == 0) {
		if (old_sa.sa_handler != SIG_DFL && old_sa.sa_handler != SIG_IGN) {
			// A custom signal handler is already active; skip registration.
			return;
		}
	}

	struct sigaction new_sa;
	memset(&new_sa, 0, sizeof(new_sa));
	new_sa.sa_sigaction = fallback_signal_handler;
	new_sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&new_sa.sa_mask);

	sigaction(SIGSEGV, &new_sa, nullptr);
	sigaction(SIGABRT, &new_sa, nullptr);
	sigaction(SIGILL, &new_sa, nullptr);
	sigaction(SIGFPE, &new_sa, nullptr);
	sigaction(SIGBUS, &new_sa, nullptr);
}

} // namespace crash_handler
