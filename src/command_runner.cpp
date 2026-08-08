#include "command_runner.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config_manager.h"
#include "crashdump_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "perf_manager.h"

namespace fs = std::filesystem;


void command_runner::apply_default_profile()
{
	bypass_sandbox_ = false;
	network_access_ = false;
	allow_display_ = false;
	home_access_ = home_access_t::hidden;
	project_dir_ = "";
	project_hash_ = "default";
	extra_rw_paths_.clear();
	extra_ro_paths_.clear();
}

void command_runner::apply_internal_profile()
{
	apply_default_profile();
	bypass_sandbox_ = true;
	bypass_crashdump_check_ = true;
}

void command_runner::add_cache_rw_exceptions()
{
	const char *home = std::getenv("HOME");
	if (!home)
		return;

	std::string home_str(home);
	if (home_str.starts_with("/tmp") || home_str.starts_with("/var/tmp"))
		return;

	std::string ccache_dir = home_str + "/.ccache";
	if (std::filesystem::exists(ccache_dir)) {
		if (std::find(extra_rw_paths_.begin(), extra_rw_paths_.end(), ccache_dir) == extra_rw_paths_.end()) {
			extra_rw_paths_.push_back(ccache_dir);
		}
	}
	std::string xdg_ccache_dir = home_str + "/.cache/ccache";
	if (std::filesystem::exists(xdg_ccache_dir)) {
		if (std::find(extra_rw_paths_.begin(), extra_rw_paths_.end(), xdg_ccache_dir) == extra_rw_paths_.end()) {
			extra_rw_paths_.push_back(xdg_ccache_dir);
		}
	}

	// Ensure ~/.cache, ~/.cache/uv, and ~/.uv exist on the host so systemd-run BindPaths source exists
	std::vector<std::string> uv_dirs = {
		home_str + "/.cache",
		home_str + "/.cache/uv",
		home_str + "/.uv"
	};
	const char *uv_cache_env = std::getenv("UV_CACHE_DIR");
	if (uv_cache_env && *uv_cache_env) {
		uv_dirs.push_back(uv_cache_env);
	}
	const char *xdg_cache_env = std::getenv("XDG_CACHE_HOME");
	if (xdg_cache_env && *xdg_cache_env) {
		uv_dirs.push_back(xdg_cache_env);
		uv_dirs.push_back(std::string(xdg_cache_env) + "/uv");
	}

	for (const auto &dir : uv_dirs) {
		std::error_code ec;
		if (!dir.starts_with("/tmp") && !dir.starts_with("/var/tmp")) {
			if (!std::filesystem::exists(dir, ec)) {
				std::filesystem::create_directories(dir, ec);
			}
			if (std::filesystem::exists(dir, ec)) {
				if (std::find(extra_rw_paths_.begin(), extra_rw_paths_.end(), dir) == extra_rw_paths_.end()) {
					extra_rw_paths_.push_back(dir);
				}
			}
		}
	}
}

void command_runner::apply_build_profile()
{
	apply_default_profile();
	network_access_ = true;
	home_access_ = home_access_t::read_only;
	project_dir_ = fs_utils::get_project_dir();
	if (project_dir_.empty()) {
		std::error_code ec;
		project_dir_ = std::filesystem::current_path(ec).string();
	}
	project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));

	add_cache_rw_exceptions();
}

void command_runner::apply_strict_agent_profile()
{
	apply_default_profile();
	network_access_ = false;
	home_access_ = home_access_t::hidden;
	project_dir_ = fs_utils::get_project_dir();
	if (project_dir_.empty()) {
		std::error_code ec;
		project_dir_ = std::filesystem::current_path(ec).string();
	}
	project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));

	add_cache_rw_exceptions();
}

static bool is_systemd_user_bus_available()
{
	const char *bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
	if (bus && *bus) {
		return true;
	}
	const char *runtime = std::getenv("XDG_RUNTIME_DIR");
	if (runtime && *runtime) {
		std::error_code ec;
		std::string bus_path = std::string(runtime) + "/bus";
		if (std::filesystem::exists(bus_path, ec)) {
			return true;
		}
	}
	return false;
}

// Builds the static set of systemd-run hardening flags that the sandbox always applies.
// The kernel-hardening pair (ProtectKernelTunables + ProtectKernelModules) is included
// only when requested. Both the runtime probe and the real build_command use this same helper
// so the probe reproduces the exact flag combination the runner will use; the kernel pair can
// fail when combined with the full set even though each works in isolation (observed on some
// Ubuntu hosts), so they are probed together.
static std::string sandbox_hardening_flags(bool include_kernel_pair)
{
	std::string flags =
		"-p ProtectSystem=strict "
		"-p PrivateTmp=true ";
	if (include_kernel_pair) {
		flags += "-p ProtectKernelTunables=true ";
		flags += "-p ProtectKernelModules=true ";
	}
	flags +=
		"-p MemoryDenyWriteExecute=true "
		"-p ProtectControlGroups=true "
		"-p RestrictRealtime=true ";
	return flags;
}

// Runs `systemd-run` with the given hardening flags and returns true iff the unit starts and
// the child (the `true` binary) exits 0. All output is logged so failures are diagnosable.
static bool probe_run_with_flags(const std::string &flags, const std::string &tag)
{
	if (!is_systemd_user_bus_available()) {
		event_logger::get_instance().log("[command_runner] {} probe: systemd user bus unavailable", tag);
		return false;
	}

	const std::string probe_cmd =
		"systemd-run --user --wait --pipe --quiet " +
		flags + "true 2>&1";

	event_logger::get_instance().log("[command_runner] {} probe: running '{}'", tag, probe_cmd);

	// Capture diagnostic output (stderr redirected to stdout above) and the wait status.
	// Draining is required so popen's waitpid completes.
	FILE *pipe_handle = popen(probe_cmd.c_str(), "r");
	if (!pipe_handle) {
		event_logger::get_instance().log("[command_runner] {} probe: popen failed (errno={})", tag, errno);
		return false;
	}

	std::array<char, 512> buf{};
	while (fgets(buf.data(), static_cast<int>(buf.size()), pipe_handle) != nullptr) {
		event_logger::get_instance().log("[command_runner] {} probe output: {}", tag, std::string(buf.data()));
	}
	int status = pclose(pipe_handle);

	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (WIFEXITED(status)) {
		event_logger::get_instance().log("[command_runner] {} probe: exited with code {}", tag, WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		event_logger::get_instance().log("[command_runner] {} probe: killed by signal {}", tag, WTERMSIG(status));
	} else {
		event_logger::get_instance().log("[command_runner] {} probe: abnormal wait status {}", tag, status);
	}
	event_logger::get_instance().log("[command_runner] {} probe: {}", tag, ok ? "SUPPORTED" : "NOT SUPPORTED");
	return ok;
}

// Determines whether the kernel-hardening pair (ProtectKernelTunables + ProtectKernelModules)
// can be used with the full sandbox flag set. Guarantees the combined probe runs at most once per
// process, safely across threads.
static bool is_kernel_pair_supported()
{
	static const bool supported = []() {
		// Positive probe (with the kernel pair present). If this works, we keep the pair.
		if (probe_run_with_flags(sandbox_hardening_flags(true), "kernel-hardening")) {
			return true;
		}

		// Negative result: run a *positive control* without the kernel pair to confirm the rest of
		// the flag set is fine, so we know it is specifically the kernel pair causing the failure and
		// not the base flags. This is purely diagnostic.
		probe_run_with_flags(sandbox_hardening_flags(false), "kernel-hardening-without-pair");
		return false;
	}();

	return supported;
}

std::string command_runner::inject_unsandboxed_environment(const std::string &raw_command) const
{
	if (!enable_crash_catcher_ && perf_dir_.empty()) {
		return raw_command;
	}

	std::string env_prefix;
	std::string dump_dir = fs_utils::get_project_dump_dir();
	std::string lib_path = fs_utils::get_turbocatch_lib_path();

	if (fs::exists(lib_path)) {
		env_prefix += "LD_PRELOAD=" + fs_utils::escape_shell_arg(lib_path) + " ";
	}
	if (!dump_dir.empty()) {
		env_prefix += "TURBOSTAR_DUMP_DIR=" + fs_utils::escape_shell_arg(dump_dir) + " ";
	}
	if (!crash_cookie_.empty()) {
		env_prefix += "TURBOSTAR_CRASH_COOKIE=" + fs_utils::escape_shell_arg(crash_cookie_) + " ";
	}
	if (!perf_dir_.empty()) {
		env_prefix += "TURBOSTAR_PERF_DIR=" + fs_utils::escape_shell_arg(perf_dir_) + " ";
	}

	return env_prefix + raw_command;
}

std::string command_runner::build_command(const std::string &raw_command) const
{
	if (bypass_sandbox_ && !config_manager::get_instance().is_paranoid_mode()) {
		return inject_unsandboxed_environment(raw_command);
	}

	if (std::getenv("TURBOSTAR_SANDBOXED")) {
		event_logger::get_instance().log("[command_runner] Already running inside outer sandbox, bypassing nested systemd-run for: '{}'", raw_command);
		return inject_unsandboxed_environment(raw_command);
	}

	if (!is_systemd_user_bus_available()) {
		if (config_manager::get_instance().is_paranoid_mode()) {
			event_logger::get_instance().log("[command_runner] ERROR: Systemd user D-Bus bus unavailable in Paranoid Mode for command: '{}'", raw_command);
		} else {
			event_logger::get_instance().log("[command_runner] WARNING: Systemd user D-Bus bus unavailable. Fallback execution triggered for command: '{}'", raw_command);
			return inject_unsandboxed_environment(raw_command);
		}
	}

	static std::atomic<int> unit_counter{0};
	auto now = std::chrono::steady_clock::now().time_since_epoch().count();
	std::string random_suffix = std::to_string(now) + "-" + std::to_string(unit_counter++);
	std::string unit_name = "turbostar-project-" + project_hash_ + "-" + random_suffix;

	std::string cmd = "systemd-run --user ";
	if (use_pty_) {
		cmd += "--pty ";
	} else {
		cmd += "--pipe ";
	}
	cmd += "--wait --quiet ";
	cmd += "--unit=" + fs_utils::escape_shell_arg(unit_name) + " ";
	// The kernel-hardening pair (ProtectKernelTunables + ProtectKernelModules) is a runtime
	// capability: on some hosts (e.g. Ubuntu) it can fail with a security violation (218) when
	// combined with the full hardening set. Gate it on a one-time runtime probe (which itself
	// reproduces the full flag combination) so it is kept wherever it works and omitted only where it
	// cannot be satisfied.
	cmd += sandbox_hardening_flags(is_kernel_pair_supported());
	cmd += "-p Environment=TURBOSTAR_SANDBOXED=1 ";

	if (!network_access_ && !allow_display_) {
		cmd += "-p PrivateNetwork=true ";
	}

	if (allow_display_) {
		const char *display_env = std::getenv("DISPLAY");
		if (display_env) {
			cmd += "-p " + fs_utils::escape_shell_arg("Environment=DISPLAY=" + std::string(display_env)) + " ";
		}
		cmd += "-p " + fs_utils::escape_shell_arg("Environment=SDL_VIDEODRIVER=x11") + " ";

		if (fs::exists("/tmp/.X11-unix")) {
			cmd += "-p BindReadOnlyPaths=/tmp/.X11-unix ";
		}

		const char *xauth_env = std::getenv("XAUTHORITY");
		std::string xauth_path;
		if (xauth_env) {
			xauth_path = xauth_env;
		} else {
			const char *home_env = std::getenv("HOME");
			if (home_env) {
				xauth_path = std::string(home_env) + "/.Xauthority";
			}
		}

		if (!xauth_path.empty() && fs::exists(xauth_path)) {
			cmd += "-p " + fs_utils::escape_shell_arg("BindReadOnlyPaths=" + xauth_path + ":/tmp/.Xauthority") + " ";
			cmd += "-p " + fs_utils::escape_shell_arg("Environment=XAUTHORITY=/tmp/.Xauthority") + " ";
		}

		const char *home_env = std::getenv("HOME");
		if (home_env) {
			std::string home(home_env);
			if (!home.starts_with("/tmp") && !home.starts_with("/var/tmp")) {
				std::string mesa_cache = home + "/.cache/mesa_shader_cache";
				std::error_code ec;
				if (!fs::exists(mesa_cache, ec)) {
					fs::create_directories(mesa_cache, ec);
				}
				if (fs::exists(mesa_cache, ec)) {
					if (home_access_ == home_access_t::hidden) {
						cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(mesa_cache) + " ";
					} else {
						cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(mesa_cache) + " ";
					}
				}
			}
		}
	}

	if (home_access_ == home_access_t::hidden) {
		cmd += "-p ProtectHome=tmpfs ";
	} else if (home_access_ == home_access_t::read_only) {
		cmd += "-p ProtectHome=read-only ";
	}

	if (!project_dir_.empty()) {
		cmd += "-p WorkingDirectory=" + fs_utils::escape_shell_arg(project_dir_) + " ";
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(project_dir_) + " ";
		} else {
			cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(project_dir_) + " ";
		}
	}

	// Only inject dump dirs and LD_PRELOAD if we are actually sandboxing AND
	// the caller explicitly opted into the LD_PRELOAD crash catcher.
	// This breaks recursion loops where internal profiles (git rev-parse)
	// bypass the sandbox but used to still call get_project_dump_dir().
	if (!bypass_sandbox_ && (enable_crash_catcher_ || !perf_dir_.empty())) {
		std::string dump_dir = fs_utils::get_project_dump_dir();
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(dump_dir) + " ";
		} else {
			cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(dump_dir) + " ";
		}

		// Inject the LD_PRELOAD crash handler
		std::string lib_path = fs_utils::get_turbocatch_lib_path();
		cmd += "-p " + fs_utils::escape_shell_arg("Environment=LD_PRELOAD=" + lib_path) + " ";
		cmd += "-p " + fs_utils::escape_shell_arg("Environment=TURBOSTAR_DUMP_DIR=" + dump_dir) + " ";
		if (!crash_cookie_.empty()) {
			cmd += "-p " + fs_utils::escape_shell_arg("Environment=TURBOSTAR_CRASH_COOKIE=" + crash_cookie_) + " ";
		}
		if (!perf_dir_.empty()) {
			cmd += "-p " + fs_utils::escape_shell_arg("Environment=TURBOSTAR_PERF_DIR=" + perf_dir_) + " ";
			if (home_access_ == home_access_t::hidden) {
				cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(perf_dir_) + " ";
			} else {
				cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(perf_dir_) + " ";
			}
		}

		// Ensure the directory containing libturbocatch.so is accessible (at least read-only)
		std::string lib_dir = std::filesystem::path(lib_path).parent_path().string();
		bool is_inside_project = false;
		if (!project_dir_.empty()) {
			std::string abs_proj = std::filesystem::absolute(project_dir_).string();
			std::string abs_lib = std::filesystem::absolute(lib_dir).string();
			if (abs_proj.empty() || abs_proj.back() != '/') {
				abs_proj += "/";
			}
			if (abs_lib.starts_with(abs_proj) || std::filesystem::absolute(lib_dir) == std::filesystem::absolute(project_dir_)) {
				is_inside_project = true;
			}
		}

		if (!is_inside_project) {
			if (home_access_ == home_access_t::hidden) {
				cmd += "-p BindReadOnlyPaths=" + fs_utils::escape_shell_arg(lib_dir) + " ";
			} else {
				cmd += "-p ReadOnlyPaths=" + fs_utils::escape_shell_arg(lib_dir) + " ";
			}
		}
	}

	for (const auto &p : extra_rw_paths_) {
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(p) + " ";
		} else {
			cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(p) + " ";
		}
	}
	for (const auto &p : extra_ro_paths_) {
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindReadOnlyPaths=" + fs_utils::escape_shell_arg(p) + " ";
		} else {
			cmd += "-p ReadOnlyPaths=" + fs_utils::escape_shell_arg(p) + " ";
		}
	}

	// Mask sensitive files natively at the kernel level
	std::vector<std::string> inaccessible_paths;

	const char *home_env = std::getenv("HOME");
	if (home_env) {
		std::string home(home_env);

		// Expose ccache directories if they exist so compilation works inside sandbox
		std::vector<std::string> ccache_paths = {home + "/.cache/ccache", home + "/.ccache"};
		const char *ccache_env = std::getenv("CCACHE_DIR");
		if (ccache_env) {
			ccache_paths.push_back(ccache_env);
		}
		for (const auto &p : ccache_paths) {
			if (fs::exists(p)) {
				if (home_access_ == home_access_t::hidden) {
					cmd += "-p BindPaths=" + fs_utils::escape_shell_arg(p) + " ";
				} else {
					cmd += "-p ReadWritePaths=" + fs_utils::escape_shell_arg(p) + " ";
				}
			}
		}

		std::vector<std::string> sensitive = {home + "/.ssh",
						      home + "/.env",
						      home + "/.aws",
						      home + "/.gnupg",
						      home + "/.gemini/keys",
						      home + "/.cache/turbostar/servers.json",
						      home + "/.cache/turbostar/models.json"};
		for (const auto &s : sensitive) {
			if (fs::exists(s)) {
				inaccessible_paths.push_back("-" + s);
			}
		}
	}

	if (!project_dir_.empty()) {
		std::vector<std::string> sensitive = {project_dir_ + "/.env", project_dir_ + "/.ssh"};
		for (const auto &s : sensitive) {
			if (fs::exists(s)) {
				inaccessible_paths.push_back("-" + s);
			}
		}
	}

	for (const auto &p : inaccessible_paths) {
		cmd += "-p InaccessiblePaths=" + fs_utils::escape_shell_arg(p) + " ";
	}

	cmd += "-- bash -c " + fs_utils::escape_shell_arg(raw_command);
	return cmd;
}

int command_runner::execute(const std::string &command)
{
	start_time_ = std::chrono::steady_clock::now();
	timed_out_ = false;
	std::string final_command = build_command(command);
	if (final_command.find("2>") == std::string::npos && final_command.find(">&") == std::string::npos) {
		final_command += " 2>&1";
	}

	event_logger::get_instance().log("[command_runner] Executing raw_command: '{}'", command);
	event_logger::get_instance().log("[command_runner] Sandboxed final_command: '{}'", final_command);

	int pipefd[2];
	if (pipe(pipefd) < 0) {
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}

	if (pid == 0) {
		// Child process
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);

		// Create a process group so we can kill any descendant processes on cancel
		setpgid(0, 0);

		execl("/bin/sh", "sh", "-c", final_command.c_str(), nullptr);
		_exit(127);
	}

	// Parent process
	close(pipefd[1]);

	// Set read end of pipe to non-blocking
	int flags = fcntl(pipefd[0], F_GETFL, 0);
	fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

	std::array<char, 4096> buffer;
	bool canceled = false;

	while (true) {
		// Check cancellation first
		if (!should_continue()) {
			canceled = true;
			break;
		}

		struct pollfd pfd;
		pfd.fd = pipefd[0];
		pfd.events = POLLIN | POLLHUP;

		int ret = poll(&pfd, 1, 100); // 100ms timeout
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			break; // error
		}
		if (ret == 0) {
			continue; // check should_continue() again
		}

		if (pfd.revents & POLLIN) {
			ssize_t bytes_read = read(pipefd[0], buffer.data(), buffer.size());
			if (bytes_read > 0) {
				on_output_chunk(std::string(buffer.data(), bytes_read));
			} else if (bytes_read == 0) {
				break; // EOF
			} else {
				if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
					continue;
				break; // error
			}
		}

		if (pfd.revents & POLLHUP) {
			// Read any remaining data
			ssize_t bytes_read;
			while ((bytes_read = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
				on_output_chunk(std::string(buffer.data(), bytes_read));
			}
			break;
		}
	}

	if (canceled) {
		// Kill child process group using negative pid
		kill(-pid, SIGKILL);
	}

	close(pipefd[0]);

	int status = 0;
	waitpid(pid, &status, 0);

	int final_exit_code = -1;
	if (WIFEXITED(status)) {
		final_exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		final_exit_code = 128 + WTERMSIG(status);
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time_).count();
	event_logger::get_instance().log("[command_runner] Finished command (exit_code={}, elapsed={}ms): '{}'", final_exit_code, elapsed, command);

	on_child_exit();

	return final_exit_code;
}

void command_runner::on_child_exit()
{
	if (!bypass_crashdump_check_ && !project_hash_.empty()) {
		last_crashdumps_report_ = crashdump_manager::get_instance().refresh(project_hash_);
	}
	if (!perf_dir_.empty()) {
		turbostar::perf_manager::get_instance().parse_and_resolve(perf_dir_, 0, run_id_, true);
	}
}

void sync_command_runner::on_output_chunk(const std::string &chunk)
{
	line_buffer_ += chunk;
	size_t pos;
	while ((pos = line_buffer_.find('\n')) != std::string::npos) {
		std::string line = line_buffer_.substr(0, pos);
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		on_output_line(line);
		line_buffer_ = line_buffer_.substr(pos + 1);
	}
}

std::string sync_command_runner::execute_and_get_output(const std::string &command)
{
	full_output_.clear();
	line_buffer_.clear();
	exit_code_ = execute(command);
	if (!line_buffer_.empty()) {
		if (line_buffer_.back() == '\r')
			line_buffer_.pop_back();
		on_output_line(line_buffer_);
		line_buffer_.clear();
	}
	return full_output_;
}

void sync_command_runner::on_output_line(const std::string &line)
{
	full_output_ += line + "\n";
}

bool command_runner::should_continue() const
{
	if (project_manager::get_instance().is_exiting()) {
		return false;
	}
	if (timeout_seconds_ > 0) {
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count() > timeout_seconds_) {
			timed_out_ = true;
			return false;
		}
	}
	return true;
}