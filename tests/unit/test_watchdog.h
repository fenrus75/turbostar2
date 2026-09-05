#pragma once
#define UNW_LOCAL_ONLY
#include <chrono>
#include <filesystem>
#include <iostream>
#include <libunwind.h>
#include <memory>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define HAS_CXXABI
#endif

#include "agentlib/command_registry.h"
#include "agentlib/skill_manager.h"
#include "agentlib/tool_registry.h"
#include "filter_registry.h"
#include "pluginloader.h"

namespace test_watchdog {

static inline std::chrono::steady_clock::time_point &get_test_start_time()
{
	static std::chrono::steady_clock::time_point start_tp = std::chrono::steady_clock::now();
	return start_tp;
}

static inline double get_elapsed_seconds()
{
	auto now = std::chrono::steady_clock::now();
	std::chrono::duration<double> dur = now - get_test_start_time();
	return dur.count();
}

static inline long get_current_tid()
{
#if defined(__linux__) && defined(SYS_gettid)
	return static_cast<long>(syscall(SYS_gettid));
#else
	return static_cast<long>(getpid());
#endif
}

class scoped_test_home {
      public:
	explicit scoped_test_home(const std::string &test_prefix = "test_home")
	{
		char pid_buf[32];
		snprintf(pid_buf, sizeof(pid_buf), "_%d", static_cast<int>(getpid()));
		temp_home_ = (std::filesystem::temp_directory_path() / (test_prefix + pid_buf)).string();
		std::filesystem::create_directories(temp_home_ + "/.cache/turbostar");
		setenv("HOME", temp_home_.c_str(), 1);

		// Preserve DBUS and XDG_RUNTIME_DIR so systemd-run --user connects to user session bus in test home
		const char *dbus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
		if (dbus && *dbus) {
			setenv("DBUS_SESSION_BUS_ADDRESS", dbus, 1);
		}
		const char *xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
		if (xdg_runtime && *xdg_runtime) {
			setenv("XDG_RUNTIME_DIR", xdg_runtime, 1);
		}

		// Ensure git identity is available in isolated test home directories
		setenv("GIT_AUTHOR_NAME", "Test User", 1);
		setenv("GIT_AUTHOR_EMAIL", "test@example.com", 1);
		setenv("GIT_COMMITTER_NAME", "Test User", 1);
		setenv("GIT_COMMITTER_EMAIL", "test@example.com", 1);
	}

	~scoped_test_home()
	{
		// Keep directory intact during process shutdown to avoid race conditions with static destructors
	}

	const std::string &get_path() const { return temp_home_; }

      private:
	std::string temp_home_;
};

static inline std::unique_ptr<scoped_test_home> &get_global_test_home()
{
	static std::unique_ptr<scoped_test_home> instance;
	return instance;
}

inline void isolate_home(const std::string &prefix = "test_home")
{
	get_global_test_home() = std::make_unique<scoped_test_home>(prefix);
}

inline void init_singletons()
{
	(void)::command_registry::get_instance();
	(void)agentlib::skill_manager::get_instance();
	(void)agentlib::filter_registry::get_instance();
	(void)agentlib::tool_registry::get_instance();
}

inline void init_plugin_environment()
{
	init_singletons();
	if (!getenv("TURBOSTAR_PLUGIN_DIR")) {
		if (std::filesystem::exists("./build/src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "./build/src/plugins", 1);
		} else if (std::filesystem::exists("./src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "./src/plugins", 1);
		} else if (std::filesystem::exists("../src/plugins")) {
			setenv("TURBOSTAR_PLUGIN_DIR", "../src/plugins", 1);
		}
	}
	plugin_loader::get_instance().load_all_plugins();
}

static inline void print_stack_trace()
{
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_getcontext(&uc);
	if (unw_init_local(&cursor, &uc) == 0) {
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
}

static void alarm_handler(int sig, siginfo_t *info, void *ucontext)
{
	(void)sig; (void)info; (void)ucontext;
	char msg[256];
	snprintf(msg, sizeof(msg), "\n*** WATCHDOG TIMEOUT (Thread ID: %ld, Elapsed: %.3fs) ***\nStack trace:\n",
		 get_current_tid(), get_elapsed_seconds());
	write(STDOUT_FILENO, msg, strlen(msg));
	print_stack_trace();
	_exit(128 + SIGALRM);
}

inline struct sigaction g_prev_crash_sa[32];
inline bool g_has_prev_crash_sa[32]{false};

static void crash_handler(int sig, siginfo_t *info, void *ucontext)
{
	const char *sig_name = "UNKNOWN";
	if (sig == SIGSEGV) sig_name = "SIGSEGV - Segmentation Fault";
	else if (sig == SIGABRT) sig_name = "SIGABRT - Aborted";
	else if (sig == SIGILL) sig_name = "SIGILL - Illegal Instruction";
	else if (sig == SIGFPE) sig_name = "SIGFPE - Floating Point Exception";
	else if (sig == SIGBUS) sig_name = "SIGBUS - Bus Error";

	char msg[256];
	snprintf(msg, sizeof(msg), "\n*** TEST CRASHED (Signal %d: %s, Thread ID: %ld, Elapsed: %.3fs) ***\nStack trace:\n",
		 sig, sig_name, get_current_tid(), get_elapsed_seconds());
	write(STDOUT_FILENO, msg, strlen(msg));
	print_stack_trace();

	// If a previous handler was installed (e.g. libturbocatch.so crash dump collector), chain to it!
	if (sig >= 0 && sig < 32 && g_has_prev_crash_sa[sig]) {
		const struct sigaction &prev = g_prev_crash_sa[sig];
		void *handler_ptr = (void *)prev.sa_sigaction;
		void *crash_handler_ptr = (void *)crash_handler;
		if (handler_ptr != nullptr && handler_ptr != crash_handler_ptr) {
			if ((prev.sa_flags & SA_SIGINFO) && prev.sa_sigaction) {
				prev.sa_sigaction(sig, info, ucontext);
				return;
			} else if (!(prev.sa_flags & SA_SIGINFO) && prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN) {
				prev.sa_handler(sig);
				return;
			}
		}
	}

	// Restore default handler and re-raise signal to cleanly terminate process
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
	_exit(128 + sig);
}

inline void setup_watchdog(unsigned int seconds = 30, bool catch_crashes = true, bool auto_isolate_home = true)
{
	if (auto_isolate_home && !get_global_test_home()) {
		isolate_home();
	}

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
		int fatal_sigs[] = {SIGSEGV, SIGABRT, SIGILL, SIGFPE, SIGBUS};
		for (int s : fatal_sigs) {
			struct sigaction prev_sa;
			memset(&prev_sa, 0, sizeof(prev_sa));
			if (sigaction(s, &crash_sa, &prev_sa) == 0) {
				g_prev_crash_sa[s] = prev_sa;
				g_has_prev_crash_sa[s] = true;
			}
		}
	}
}

} // namespace test_watchdog

#include <dlfcn.h>
#if defined(__GLIBC__) || defined(__linux__)
extern "C" __attribute__((weak)) __attribute__((noreturn))
void __assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) noexcept
{
	typedef void (*assert_fail_fn)(const char *, const char *, unsigned int, const char *);
	const char *dump_dir = getenv("TURBOSTAR_DUMP_DIR");
	if (dump_dir && *dump_dir) {
		assert_fail_fn orig = (assert_fail_fn)dlsym(RTLD_NEXT, "__assert_fail");
		if (orig) {
			orig(assertion, file, line, function);
		}
	}

	char header[512];
	snprintf(header, sizeof(header),
		 "\n================================================================================\n"
		 "*** TEST ASSERTION FAILED ***\n"
		 "File:       %s:%u\n"
		 "Function:   %s\n"
		 "Assertion:  assert(%s)\n"
		 "Thread ID:  %ld\n"
		 "Elapsed:    %.3fs\n"
		 "================================================================================\n"
		 "Call Stack:\n",
		 file ? file : "unknown",
		 line,
		 function ? function : "unknown",
		 assertion ? assertion : "unknown",
		 test_watchdog::get_current_tid(),
		 test_watchdog::get_elapsed_seconds());
	write(STDOUT_FILENO, header, strlen(header));

	test_watchdog::print_stack_trace();

	const char *footer = "================================================================================\n\n";
	write(STDOUT_FILENO, footer, strlen(footer));

	abort();
}

extern "C" __attribute__((weak)) __attribute__((noreturn))
void __assert_perror_fail(int errnum, const char *file, unsigned int line, const char *function) noexcept
{
	typedef void (*assert_perror_fail_fn)(int, const char *, unsigned int, const char *);
	const char *dump_dir = getenv("TURBOSTAR_DUMP_DIR");
	if (dump_dir && *dump_dir) {
		assert_perror_fail_fn orig = (assert_perror_fail_fn)dlsym(RTLD_NEXT, "__assert_perror_fail");
		if (orig) {
			orig(errnum, file, line, function);
		}
	}

	char header[512];
	snprintf(header, sizeof(header),
		 "\n================================================================================\n"
		 "*** TEST PERROR ASSERTION FAILED ***\n"
		 "File:       %s:%u\n"
		 "Function:   %s\n"
		 "Errno:      %d (%s)\n"
		 "Thread ID:  %ld\n"
		 "Elapsed:    %.3fs\n"
		 "================================================================================\n"
		 "Call Stack:\n",
		 file ? file : "unknown",
		 line,
		 function ? function : "unknown",
		 errnum,
		 strerror(errnum),
		 test_watchdog::get_current_tid(),
		 test_watchdog::get_elapsed_seconds());
	write(STDOUT_FILENO, header, strlen(header));

	test_watchdog::print_stack_trace();

	const char *footer = "================================================================================\n\n";
	write(STDOUT_FILENO, footer, strlen(footer));

	abort();
}
#endif
