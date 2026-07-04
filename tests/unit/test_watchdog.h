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

static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
	(void)info;
	const char *sig_name = "UNKNOWN";
	if (sig == SIGSEGV) sig_name = "SIGSEGV - Segmentation Fault";
	else if (sig == SIGABRT) sig_name = "SIGABRT - Aborted";
	else if (sig == SIGILL) sig_name = "SIGILL - Illegal Instruction";
	else if (sig == SIGFPE) sig_name = "SIGFPE - Floating Point Exception";
	else if (sig == SIGBUS) sig_name = "SIGBUS - Bus Error";

	char msg[256];
	snprintf(msg, sizeof(msg), "\n*** TEST CRASHED (Signal %d: %s) ***\nStack trace:\n", sig, sig_name);
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

	// Restore default handler and re-raise signal to cleanly terminate process
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
	_exit(128 + sig);
}

inline void setup_watchdog(unsigned int seconds = 30, bool catch_crashes = true)
{
	struct sigaction alarm_sa;
	memset(&alarm_sa, 0, sizeof(alarm_sa));
	alarm_sa.sa_sigaction = alarm_handler;
	alarm_sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&alarm_sa.sa_mask);
	sigaction(SIGALRM, &alarm_sa, nullptr);
	alarm(seconds);

	if (catch_crashes) {
		struct sigaction crash_sa;
		memset(&crash_sa, 0, sizeof(crash_sa));
		crash_sa.sa_sigaction = crash_handler;
		crash_sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
		sigemptyset(&crash_sa.sa_mask);
		sigaction(SIGSEGV, &crash_sa, nullptr);
		sigaction(SIGABRT, &crash_sa, nullptr);
		sigaction(SIGILL, &crash_sa, nullptr);
		sigaction(SIGFPE, &crash_sa, nullptr);
		sigaction(SIGBUS, &crash_sa, nullptr);
	}
}

} // namespace test_watchdog
