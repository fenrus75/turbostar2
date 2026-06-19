#pragma once
#define UNW_LOCAL_ONLY
#include <signal.h>
#include <unistd.h>
#include <libunwind.h>
#include <string.h>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define HAS_CXXABI
#endif

namespace test_watchdog {

static void alarm_handler(int sig, siginfo_t *info, void *ucontext)
{
	(void)sig; (void)info;
	const char *msg = "\n*** WATCHDOG TIMEOUT ***\nStack trace:\n";
	write(STDOUT_FILENO, msg, strlen(msg));

	unw_cursor_t cursor;
	if (unw_init_local(&cursor, reinterpret_cast<unw_context_t *>(ucontext)) == 0) {
		int frame = 0;
		do {
			unw_word_t ip;
			if (unw_get_reg(&cursor, UNW_REG_IP, &ip) == 0) {
				char ip_str[64];
				snprintf(ip_str, sizeof(ip_str), "  #%d 0x%lx", frame, (unsigned long)ip);
				write(STDOUT_FILENO, ip_str, strlen(ip_str));

				char symbol[256];
				unw_word_t offset;
				if (unw_get_proc_name(&cursor, symbol, sizeof(symbol), &offset) == 0) {
					const char *sym_to_write = symbol;
#ifdef HAS_CXXABI
					int status = 0;
					char *demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
					if (status == 0 && demangled) {
						sym_to_write = demangled;
					}
#endif
					write(STDOUT_FILENO, " in ", 4);
					write(STDOUT_FILENO, sym_to_write, strlen(sym_to_write));
					char offset_str[32];
					snprintf(offset_str, sizeof(offset_str), " + 0x%lx", (unsigned long)offset);
					write(STDOUT_FILENO, offset_str, strlen(offset_str));
#ifdef HAS_CXXABI
					if (status == 0 && demangled) {
						free(demangled);
					}
#endif
				}
				write(STDOUT_FILENO, "\n", 1);
			}
			frame++;
		} while (unw_step(&cursor) > 0 && frame < 128);
	} else {
		const char *err_msg = "  Failed to initialize stack unwinding via libunwind.\n";
		write(STDOUT_FILENO, err_msg, strlen(err_msg));
	}
	_exit(128 + SIGALRM);
}

inline void setup_watchdog(unsigned int seconds = 30)
{
	struct sigaction new_sa;
	memset(&new_sa, 0, sizeof(new_sa));
	new_sa.sa_sigaction = alarm_handler;
	new_sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&new_sa.sa_mask);
	sigaction(SIGALRM, &new_sa, nullptr);
	alarm(seconds);
}

} // namespace test_watchdog
