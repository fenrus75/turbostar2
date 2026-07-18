#define UNW_LOCAL_ONLY
#include "crash_handler.h"
#include "fs_utils.h"
#include <libunwind.h>
#include <signal.h>
#include <ucontext.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>
#include <cstdlib>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#include <stdlib.h>
#define HAS_CXXABI
#endif

namespace crash_handler
{

int crash_fd = -1;
static char crash_filepath[512] = "";

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

static void safe_write(const char *msg)
{
	if (!msg) return;
	size_t len = safe_strlen(msg);
	write(STDERR_FILENO, msg, len);
	if (crash_fd != -1) {
		write(crash_fd, msg, len);
	}
}

static void cleanup_crash_file()
{
	if (crash_fd != -1) {
		close(crash_fd);
		crash_fd = -1;
	}
	if (safe_strlen(crash_filepath) > 0) {
		unlink(crash_filepath);
		crash_filepath[0] = '\0';
	}
}

static void setup_crash_file()
{
	namespace fs = std::filesystem;
	try {
		std::string cache_dir = fs_utils::get_global_cache_dir();
		fs::path crash_dir = fs::path(cache_dir) / "crashes";

		// 1. Scan directory for size 0 files and delete them
		if (fs::exists(crash_dir)) {
			for (auto &p : fs::directory_iterator(crash_dir)) {
				if (p.is_regular_file() && fs::file_size(p.path()) == 0) {
					fs::remove(p.path());
				}
			}
		} else {
			fs::create_directories(crash_dir);
		}

		// 2. Create temp file via mkstemp
		std::string temp_template = (crash_dir / "crash_XXXXXX").string();
		if (temp_template.size() < sizeof(crash_filepath)) {
			strncpy(crash_filepath, temp_template.c_str(), sizeof(crash_filepath));
			int fd = mkstemp(crash_filepath);
			if (fd != -1) {
				crash_fd = fd;
				atexit(cleanup_crash_file);
			} else {
				crash_filepath[0] = '\0';
			}
		}
	} catch (...) {
		// Ignore any filesystem errors to avoid crashing during crash handler setup
	}
}

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

	if (safe_strlen(crash_filepath) > 0) {
		pid_t parent_pid = getpid();
		pid_t helper_pid = fork();
		if (helper_pid == 0) {
			char pid_str[32];
			safe_itoa(parent_pid, pid_str, sizeof(pid_str));

			// Resolve helper binary absolute path relative to current running executable
			char exe_dir[512];
			ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
			if (len > 0) {
				exe_dir[len] = '\0';
				char *last_slash = nullptr;
				for (int i = 0; i < len; ++i) {
					if (exe_dir[i] == '/') {
						last_slash = &exe_dir[i];
					}
				}
				if (last_slash) {
					*(last_slash + 1) = '\0';
				}

				char helper_path[1024];
				helper_path[0] = '\0';
				strncpy(helper_path, exe_dir, sizeof(helper_path) - 1);
				strncat(helper_path, "turbostar-crashprocess", sizeof(helper_path) - strlen(helper_path) - 1);

				execl(helper_path, "turbostar-crashprocess", crash_filepath, pid_str, nullptr);
			}

			// Fallback if not found locally
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

void install_fallback_handler()
{
	setup_crash_file();

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
