#include "command_runner.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config_manager.h"
#include "crashdump_manager.h"
#include "fs_utils.h"

namespace fs = std::filesystem;

static std::string get_turbocatch_lib_path()
{
	static std::string cached_path = []() {
		std::vector<std::string> search_paths = {"/usr/lib/x86_64-linux-gnu/libturbocatch.so", "/usr/lib64/libturbocatch.so",
							 fs::absolute(fs::path("build") / "libturbocatch.so").string()};
		for (const auto &path : search_paths) {
			if (fs::exists(path)) {
				return path;
			}
		}
		return fs::absolute(fs::path("build") / "libturbocatch.so").string();
	}();
	return cached_path;
}

std::string command_runner::get_repository_root()
{
	std::string cmd = "git rev-parse --show-toplevel 2>/dev/null";
	sync_command_runner runner;
	runner.apply_internal_profile();
	std::string result = runner.execute_and_get_output(cmd);
	if (!result.empty() && result.back() == '\n') {
		result.pop_back();
	}
	if (result.empty()) {
		return fs::current_path().string();
	}
	return result;
}

void command_runner::apply_default_profile()
{
	bypass_sandbox_ = false;
	network_access_ = false;
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

void command_runner::apply_build_profile()
{
	apply_default_profile();
	network_access_ = true;
	home_access_ = home_access_t::read_only;
	project_dir_ = get_repository_root();
	project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));

	// Allow ccache write access if the user has it configured
	const char *home = std::getenv("HOME");
	if (home) {
		std::string ccache_dir = std::string(home) + "/.ccache";
		if (std::filesystem::exists(ccache_dir)) {
			extra_rw_paths_.push_back(ccache_dir);
		}
		std::string xdg_ccache_dir = std::string(home) + "/.cache/ccache";
		if (std::filesystem::exists(xdg_ccache_dir)) {
			extra_rw_paths_.push_back(xdg_ccache_dir);
		}
	}
}

void command_runner::apply_strict_agent_profile()
{
	apply_default_profile();
	network_access_ = false;
	home_access_ = home_access_t::hidden;
	project_dir_ = get_repository_root();
	project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));
}

std::string command_runner::build_command(const std::string &raw_command) const
{
	if (bypass_sandbox_ && !config_manager::get_instance().is_paranoid_mode()) {
		return raw_command;
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
	cmd += "--unit=" + unit_name + " ";
	cmd += "-p ProtectSystem=strict ";
	cmd += "-p PrivateTmp=true ";
	cmd += "-p PrivateDevices=true ";
	cmd += "-p ProtectKernelTunables=true ";
	cmd += "-p ProtectKernelModules=true ";
	cmd += "-p MemoryDenyWriteExecute=true ";
	cmd += "-p ProtectControlGroups=true ";
	cmd += "-p RestrictRealtime=true ";

	if (!network_access_) {
		cmd += "-p PrivateNetwork=true ";
	}

	if (home_access_ == home_access_t::hidden) {
		cmd += "-p ProtectHome=tmpfs ";
	} else if (home_access_ == home_access_t::read_only) {
		cmd += "-p ProtectHome=read-only ";
	}

	if (!project_dir_.empty()) {
		cmd += "-p WorkingDirectory=" + project_dir_ + " ";
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindPaths=" + project_dir_ + " ";
		} else {
			cmd += "-p ReadWritePaths=" + project_dir_ + " ";
		}
	}

	// Only inject dump dirs and LD_PRELOAD if we are actually sandboxing AND
	// the caller explicitly opted into the LD_PRELOAD crash catcher.
	// This breaks recursion loops where internal profiles (git rev-parse)
	// bypass the sandbox but used to still call get_project_dump_dir().
	if (!bypass_sandbox_ && enable_crash_catcher_) {
		std::string dump_dir = fs_utils::get_project_dump_dir();
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindPaths=" + dump_dir + " ";
		} else {
			cmd += "-p ReadWritePaths=" + dump_dir + " ";
		}

		// Inject the LD_PRELOAD crash handler
		std::string lib_path = get_turbocatch_lib_path();
		cmd += "-p Environment=\"LD_PRELOAD=" + lib_path + "\" ";
		cmd += "-p Environment=\"TURBOSTAR_DUMP_DIR=" + dump_dir + "\" ";
	}

	for (const auto &p : extra_rw_paths_) {
		cmd += "-p ReadWritePaths=" + p + " ";
	}
	for (const auto &p : extra_ro_paths_) {
		if (home_access_ == home_access_t::hidden) {
			cmd += "-p BindReadOnlyPaths=" + p + " ";
		} else {
			cmd += "-p ReadOnlyPaths=" + p + " ";
		}
	}

	// Mask sensitive files natively at the kernel level
	std::vector<std::string> inaccessible_paths;

	const char *home_env = std::getenv("HOME");
	if (home_env) {
		std::string home(home_env);
		std::vector<std::string> sensitive = {home + "/.ssh", home + "/.env", home + "/.aws", home + "/.gnupg",
						      home + "/.gemini/keys", home + "/.cache/turbostar/models.json"};
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
		cmd += "-p InaccessiblePaths=" + p + " ";
	}

	// Escape single quotes in the raw command
	std::string escaped_command;
	for (char c : raw_command) {
		if (c == '\'')
			escaped_command += "'\\''";
		else
			escaped_command += c;
	}

	cmd += "-- bash -c '" + escaped_command + "'";
	return cmd;
}

int command_runner::execute(const std::string &command)
{
	std::string final_command = build_command(command) + " 2>&1"; // Changed from 2>/dev/null to see errors
	FILE *pipe = popen(final_command.c_str(), "r");

	if (!pipe) {
		return -1; // Failed to start
	}

#include <unistd.h> // needed for read()
	int fd = fileno(pipe);
	std::array<char, 4096> buffer; // Larger buffer for raw reads

	while (should_continue()) {
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;

		int ret = poll(&pfd, 1, 100); // 100ms timeout
		if (ret < 0) {
			break; // poll error
		}
		if (ret == 0) {
			continue; // Timeout, check should_continue() again
		}

		ssize_t bytes_read = read(fd, buffer.data(), buffer.size());
		if (bytes_read <= 0) {
			break; // EOF or error
		}

		on_output_chunk(std::string(buffer.data(), bytes_read));
	}

	int exit_code = pclose(pipe);
	int status = WEXITSTATUS(exit_code);

	if (!bypass_crashdump_check_ && !project_hash_.empty()) {
		last_crashdumps_report_ = crashdump_manager::get_instance().refresh(project_hash_);
	}

	return status;
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