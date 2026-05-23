#include "crash_catcher.h"
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ucontext.h>

#define UNW_LOCAL_ONLY
#include <libunwind.h>

// Pre-allocated static buffers to avoid malloc in signal handler
static char dump_dir_base[1024] = {0};
static int dump_dir_len = 0;

// Async-signal-safe string length
static size_t safe_strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

// Async-signal-safe integer to string (base 10)
static void safe_itoa(long val, char* buf, int buf_size) {
    if (buf_size < 2) return;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    char temp[32];
    int i = 0;
    int is_neg = 0;
    
    if (val < 0) {
        is_neg = 1;
        val = -val;
    }

    while (val > 0 && i < 31) {
        temp[i++] = (val % 10) + '0';
        val /= 10;
    }
    
    if (is_neg && i < 31) {
        temp[i++] = '-';
    }
    
    int j = 0;
    while (i > 0 && j < buf_size - 1) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

// Async-signal-safe string copy
static void safe_strcpy(char* dest, const char* src, int dest_size) {
    int i = 0;
    while (src && src[i] && i < dest_size - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Async-signal-safe string concat
static void safe_strcat(char* dest, const char* src, int dest_size) {
    int len = safe_strlen(dest);
    safe_strcpy(dest + len, src, dest_size - len);
}

static void write_maps(const char* dir_path) {
    char filepath[1024] = {0};
    safe_strcpy(filepath, dir_path, sizeof(filepath));
    safe_strcat(filepath, "/maps.txt", sizeof(filepath));

    int out_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) return;

    int in_fd = open("/proc/self/maps", O_RDONLY);
    if (in_fd >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(out_fd, buf + written, n - written);
                if (w <= 0) break;
                written += w;
            }
        }
        close(in_fd);
    }
    close(out_fd);
}

static void write_registers(const char* dir_path, ucontext_t* uc) {
    char filepath[1024] = {0};
    safe_strcpy(filepath, dir_path, sizeof(filepath));
    safe_strcat(filepath, "/registers.bin", sizeof(filepath));

    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    // Write the raw ucontext_t struct. The parser will know the architecture.
    write(fd, uc, sizeof(ucontext_t));
    close(fd);
}

static void write_backtrace(const char* dir_path, ucontext_t* uc) {
    char filepath[1024] = {0};
    safe_strcpy(filepath, dir_path, sizeof(filepath));
    safe_strcat(filepath, "/stack.bin", sizeof(filepath));

    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    unw_cursor_t cursor;
    
    // We use the ucontext_t provided by the signal handler, which holds the exact state of the crashed thread
    if (unw_init_local(&cursor, (unw_context_t*)uc) < 0) {
        close(fd);
        return;
    }

    // Dump raw instruction pointers
    int max_frames = 100;
    do {
        unw_word_t ip;
        if (unw_get_reg(&cursor, UNW_REG_IP, &ip) == 0) {
            write(fd, &ip, sizeof(ip));
        }
    } while (unw_step(&cursor) > 0 && max_frames-- > 0);

    close(fd);
}

static void write_info(const char* dir_path, int sig) {
    char filepath[1024] = {0};
    safe_strcpy(filepath, dir_path, sizeof(filepath));
    safe_strcat(filepath, "/info.txt", sizeof(filepath));

    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    char buf[128] = "Signal: ";
    char sig_str[16];
    safe_itoa(sig, sig_str, sizeof(sig_str));
    safe_strcat(buf, sig_str, sizeof(buf));
    safe_strcat(buf, "\n", sizeof(buf));

    write(fd, buf, safe_strlen(buf));
    close(fd);
}

void turbocatch_handle_signal(int sig, siginfo_t* info, void* ucontext) {
    (void)info; // Suppress unused parameter warning

    // Prevent recursive crashing if our handler does something dumb
    signal(sig, SIG_DFL);

    if (dump_dir_len == 0) {
        // If not configured, just exit
        _exit(128 + sig);
    }

    ucontext_t* uc = (ucontext_t*)ucontext;

    // Create a unique directory for this crash: dumps/<pid>
    // We use PID because multiple sandboxed test processes might crash concurrently
    pid_t pid = getpid();
    char pid_str[32];
    safe_itoa(pid, pid_str, sizeof(pid_str));

    char crash_dir[1024] = {0};
    safe_strcpy(crash_dir, dump_dir_base, sizeof(crash_dir));
    safe_strcat(crash_dir, "/crash_", sizeof(crash_dir));
    safe_strcat(crash_dir, pid_str, sizeof(crash_dir));

    // Try to create the directory (0755)
    // If it fails because it exists, that's fine, we'll overwrite contents.
    mkdir(crash_dir, 0755);

    // Dump all the data
    write_info(crash_dir, sig);
    write_maps(crash_dir);
    write_registers(crash_dir, uc);
    write_backtrace(crash_dir, uc);

    // Exit immediately. Do not attempt to return to corrupted state.
    _exit(128 + sig);
}

void turbocatch_init(void) {
    // Read the target directory from the environment
    const char* env_dir = getenv("TURBOSTAR_DUMP_DIR");
    if (env_dir) {
        safe_strcpy(dump_dir_base, env_dir, sizeof(dump_dir_base));
        dump_dir_len = safe_strlen(dump_dir_base);

        // Register handlers for fatal signals
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = turbocatch_handle_signal;
        sa.sa_flags = SA_SIGINFO | SA_RESETHAND; // Provide ucontext and reset to default after first catch
        
        sigemptyset(&sa.sa_mask);

        sigaction(SIGSEGV, &sa, NULL);
        sigaction(SIGABRT, &sa, NULL);
        sigaction(SIGILL, &sa, NULL);
        sigaction(SIGFPE, &sa, NULL);
        sigaction(SIGBUS, &sa, NULL);
    }
}
