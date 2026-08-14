#define UNW_LOCAL_ONLY
#include "crash_handler.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "git_version.h"
#include <libunwind.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <cerrno>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <stdlib.h>
#define HAS_CXXABI
#endif

namespace crash_handler
{

int crash_fd = -1;
static int reserved_fd = -1;
static char crash_filepath[512] = "";
static char crashprocess_path[512] = "";

// [Signal-Safe]
// Computes string length using stack-only pointer traversal. Safe to call in signal handlers.
static size_t safe_strlen(const char *s)
{
	size_t len = 0;
	while (s && s[len]) {
		len++;
	}
	return len;
}

// [NOT Signal-Safe]
// Executed once during application setup (setup_crash_file). Resolves and caches the path
// to the turbostar-crashprocess helper binary so signal handlers can invoke it directly without
// dynamic path lookup or heap allocations at crash time.
static void resolve_crashprocess_path()
{
	namespace fs = std::filesystem;
	std::error_code ec;

	// 1. Try relative to current running executable
	char exe_buf[512];
	ssize_t len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
	if (len > 0) {
		exe_buf[len] = '\0';
		fs::path bin_dir = fs::path(exe_buf).parent_path();
		fs::path local_helper = bin_dir / "turbostar-crashprocess";
		if (fs::exists(local_helper, ec) && access(local_helper.c_str(), X_OK) == 0) {
			strncpy(crashprocess_path, local_helper.c_str(), sizeof(crashprocess_path) - 1);
			crashprocess_path[sizeof(crashprocess_path) - 1] = '\0';
			return;
		}
	}

	// 2. Try the configured Meson install path
#ifdef TURBOSTAR_CRASHPROCESS_PATH
	if (fs::exists(TURBOSTAR_CRASHPROCESS_PATH, ec) && access(TURBOSTAR_CRASHPROCESS_PATH, X_OK) == 0) {
		strncpy(crashprocess_path, TURBOSTAR_CRASHPROCESS_PATH, sizeof(crashprocess_path) - 1);
		crashprocess_path[sizeof(crashprocess_path) - 1] = '\0';
		return;
	}
#endif

	// 3. Fallback to binary name for PATH lookup
	strncpy(crashprocess_path, "turbostar-crashprocess", sizeof(crashprocess_path) - 1);
	crashprocess_path[sizeof(crashprocess_path) - 1] = '\0';
}

// [Signal-Safe]
// Formats a signed long into a caller-supplied buffer using stack storage only.
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

// [Signal-Safe]
// Formats an unsigned long hex value into a caller-supplied buffer using stack storage only.
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

// [Signal-Safe]
// Opens the crash log file lazily on demand when a crash occurs. POSIX open() is async-signal-safe.
// Closes pre-allocated reserved_fd to recover an FD slot if EMFILE occurs.
static void ensure_crash_file_open_signal_safe()
{
	if (crash_fd != -1 || safe_strlen(crash_filepath) == 0) {
		return;
	}
	int fd = open(crash_filepath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd == -1 && errno == EMFILE && reserved_fd != -1) {
		close(reserved_fd);
		reserved_fd = -1;
		fd = open(crash_filepath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	}
	if (fd != -1) {
		crash_fd = fd;
	}
}

// [Signal-Safe]
// Writes a null-terminated string to STDERR_FILENO and crash_fd using POSIX write().
static void safe_write(const char *msg)
{
	if (!msg) return;
	size_t len = safe_strlen(msg);
	write(STDERR_FILENO, msg, len);
	ensure_crash_file_open_signal_safe();
	if (crash_fd != -1) {
		write(crash_fd, msg, len);
	}
}

// [NOT Signal-Safe / atexit Handler]
// Registered via atexit(). Runs on clean process shutdown to close open file descriptors and
// remove unused zero-byte crash files.
static void cleanup_crash_file()
{
	if (reserved_fd != -1) {
		close(reserved_fd);
		reserved_fd = -1;
	}
	if (crash_fd != -1) {
		close(crash_fd);
		crash_fd = -1;
	}
	if (safe_strlen(crash_filepath) > 0) {
		std::error_code ec;
		if (std::filesystem::exists(crash_filepath, ec)) {
			auto sz = std::filesystem::file_size(crash_filepath, ec);
			if (sz > 0) {
				fprintf(stderr, "\n[Turbostar] Preserving crash diagnostic log (%llu bytes): %s\n",
					static_cast<unsigned long long>(sz), crash_filepath);
			} else {
				unlink(crash_filepath);
			}
		}
		crash_filepath[0] = '\0';
	}
}

// [NOT Signal-Safe]
// Runs during application startup (install_fallback_handler). Pre-allocates reserved_fd,
// resolves crashprocess_path, and initializes crash_filepath without creating the file.
static void setup_crash_file()
{
	namespace fs = std::filesystem;
	try {
		std::string cache_dir = fs_utils::get_global_cache_dir();
		fs::path crash_dir = fs::path(cache_dir) / "crashes";

		// 1. Scan directory for stale 0-size files (e.g. >1 hour old)
		auto now = std::filesystem::file_time_type::clock::now();
		if (fs::exists(crash_dir)) {
			for (auto &p : fs::directory_iterator(crash_dir)) {
				std::error_code ec;
				if (p.is_regular_file(ec) && fs::file_size(p.path(), ec) == 0) {
					auto last_write = fs::last_write_time(p.path(), ec);
					if (!ec && (now - last_write) > std::chrono::hours(1)) {
						fs::remove(p.path(), ec);
					}
				}
			}
		} else {
			fs::create_directories(crash_dir);
		}

		// 2. Pre-allocate dummy reserved_fd for EMFILE safety
		if (reserved_fd == -1) {
			reserved_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
		}

		// 3. Pre-resolve path to turbostar-crashprocess helper binary
		resolve_crashprocess_path();

		// 4. Format crash_filepath (with PID & timestamp) but DO NOT create the file yet (Option A)
		pid_t pid = getpid();
		long now_sec = static_cast<long>(time(nullptr));
		std::string path_str = (crash_dir / ("crash_" + std::to_string(pid) + "_" + std::to_string(now_sec) + ".txt")).string();
		if (path_str.size() < sizeof(crash_filepath)) {
			strncpy(crash_filepath, path_str.c_str(), sizeof(crash_filepath));
			crash_filepath[sizeof(crash_filepath) - 1] = '\0';
			crash_fd = -1;
			atexit(cleanup_crash_file);
		}
	} catch (...) {
		// Ignore any filesystem errors to avoid crashing during crash handler setup
	}
}

// [NOT Signal-Safe / Exception Handler]
// Uncaught exception handler registered via std::set_terminate(). Runs before std::abort()
// when a C++ exception is rethrown uncaught.
static void fallback_terminate_handler()
{
	std::string exc_info = "Unknown uncaught exception";
	std::exception_ptr exc_ptr = std::current_exception();
	if (exc_ptr) {
		try {
			std::rethrow_exception(exc_ptr);
		} catch (const std::exception &e) {
			exc_info = std::string("Uncaught std::exception: ") + e.what();
		} catch (...) {
			exc_info = "Uncaught unknown exception type";
		}
	}

	ensure_crash_file_open_signal_safe();
	if (crash_fd != -1) {
		std::string msg = "\n*** Turbostar Uncaught Exception ***\n" + exc_info + "\n";
#ifdef TURBOSTAR_GIT_HASH
		msg += "Git Commit: " TURBOSTAR_GIT_HASH "\n";
#endif
		msg += "Analysis Protocol: See docs/turbostar-crash-analysis-protocol.md\n";
		write(crash_fd, msg.c_str(), msg.length());
		event_logger::dump_recent_logs_signal_safe(crash_fd, 10);
	}
	event_logger::dump_recent_logs_signal_safe(STDERR_FILENO, 10);

	std::abort();
}

// [MUST BE STRICTLY Signal-Safe]
// Core crash signal handler for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS.
// MUST NOT perform heap allocations (malloc/new), standard stream I/O (printf/cout), or non-async-signal-safe calls.
// Uses stack-only helpers (safe_write, safe_itoa, safe_hex_toa, libunwind) and direct POSIX syscalls (open, write, fork, execl, _exit).
static void fallback_signal_handler(int sig, siginfo_t *info, void *ucontext)
{
	int is_write = 0;
	int si_code = 0;
	int is_addr_valid = 0;
	if (info) {
		si_code = info->si_code;
		if (sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE || sig == SIGILL) {
			is_addr_valid = 1;
		}
		if (sig == SIGSEGV || sig == SIGBUS) {
			ucontext_t *uc = reinterpret_cast<ucontext_t *>(ucontext);
			if (uc) {
				unsigned long err_code = uc->uc_mcontext.gregs[REG_ERR];
				if (err_code & 0x02) {
					is_write = 1;
				}
			}
		}
	}

	// Reset terminal to sane state (disable mouse/bracketed paste, show cursor)
	// We write directly to STDERR_FILENO to ensure the terminal is reset even if stdout is redirected.
	const char *reset_seq = "\033[?1002l\033[?2004l\033[?25h\033[0m\n";
	safe_write(reset_seq);

	const char *msg_prefix = "\n*** Turbostar Fallback Crash Catcher ***\nCaught signal: ";
	safe_write(msg_prefix);

	char sig_buf[16];
	safe_itoa(sig, sig_buf, sizeof(sig_buf));
	safe_write(sig_buf);

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
	safe_write(sig_name);
	safe_write("\n");

	if (is_addr_valid && info) {
		safe_write("CrashAddress: 0x");
		char addr_str[32];
		safe_hex_toa(reinterpret_cast<unsigned long>(info->si_addr), addr_str, sizeof(addr_str));
		safe_write(addr_str);
		if (sig == SIGSEGV || sig == SIGBUS) {
			if (is_write) {
				safe_write(" (write)");
			} else {
				safe_write(" (read)");
			}
		}
		safe_write("\n");
	}

	if (sig == SIGSEGV) {
		if (si_code == SEGV_MAPERR) {
			safe_write("Type: SEGV_MAPERR\n");
		} else if (si_code == SEGV_ACCERR) {
			safe_write("Type: SEGV_ACCERR\n");
		}
	}

	char exe_path[512];
	ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	if (len > 0) {
		exe_path[len] = '\0';
		safe_write("Executable: ");
		safe_write(exe_path);
		safe_write("\n");
	}

#ifdef TURBOSTAR_GIT_HASH
	safe_write("Git Commit: ");
	safe_write(TURBOSTAR_GIT_HASH);
	safe_write("\n");
#endif
	safe_write("Analysis Protocol: See docs/turbostar-crash-analysis-protocol.md\n");

	event_logger::dump_recent_logs_signal_safe(STDERR_FILENO, 10);
	if (crash_fd != -1) {
		event_logger::dump_recent_logs_signal_safe(crash_fd, 10);
	}

	safe_write("\nStack trace:\n");

	unw_cursor_t cursor;
	if (unw_init_local(&cursor, reinterpret_cast<unw_context_t *>(ucontext)) == 0) {
		int frame = 0;
		do {
			unw_word_t ip;
			if (unw_get_reg(&cursor, UNW_REG_IP, &ip) == 0) {
				safe_write("  #");
				char frame_num_str[16];
				safe_itoa(frame, frame_num_str, sizeof(frame_num_str));
				safe_write(frame_num_str);

				safe_write(" 0x");
				char ip_str[32];
				safe_hex_toa(ip, ip_str, sizeof(ip_str));
				safe_write(ip_str);

				char symbol[256];
				unw_word_t offset;
				if (unw_get_proc_name(&cursor, symbol, sizeof(symbol), &offset) == 0) {
					safe_write(" in ");
					const char *sym_to_write = symbol;
#ifdef HAS_CXXABI
					int status = 0;
					char *demangled = abi::__cxa_demangle(symbol, nullptr, nullptr, &status);
					if (status == 0 && demangled) {
						sym_to_write = demangled;
					}
#endif
					safe_write(sym_to_write);
					safe_write(" + 0x");
					char offset_str[32];
					safe_hex_toa(offset, offset_str, sizeof(offset_str));
					safe_write(offset_str);
				}
				safe_write("\n");
			}
			frame++;
		} while (unw_step(&cursor) > 0 && frame < 128);
	} else {
		const char *err_msg = "  Failed to initialize stack unwinding via libunwind.\n";
		safe_write(err_msg);
	}

	if (crash_fd != -1) {
		close(crash_fd);
		crash_fd = -1;
	}

	if (safe_strlen(crash_filepath) > 0 && safe_strlen(crashprocess_path) > 0) {
		pid_t parent_pid = getpid();
		pid_t helper_pid = fork();
		if (helper_pid == 0) {
			char pid_str[32];
			safe_itoa(parent_pid, pid_str, sizeof(pid_str));
			execl(crashprocess_path, "turbostar-crashprocess", crash_filepath, pid_str, nullptr);
			execlp("turbostar-crashprocess", "turbostar-crashprocess", crash_filepath, pid_str, nullptr);
			_exit(1);
		} else if (helper_pid > 0) {
			waitpid(helper_pid, nullptr, 0);
		}
	}

	// Restore default handler and re-raise signal to cleanly terminate process
	signal(sig, SIG_DFL);
	kill(getpid(), sig);
	_exit(128 + sig);
}

// [NOT Signal-Safe]
// Public entry point called once during application initialization to install fallback signal
// handlers and set_terminate handler.
void install_fallback_handler()
{
	setup_crash_file();

	// Install custom terminate handler to capture uncaught C++ exceptions before standard abort.
	std::set_terminate(fallback_terminate_handler);

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
