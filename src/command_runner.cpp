#include "command_runner.h"
#include "config_manager.h"
#include "coredump_manager.h"
#include <array>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>

namespace fs = std::filesystem;

std::string command_runner::get_repository_root() {
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

void command_runner::apply_default_profile() {
    bypass_sandbox_ = false;
    network_access_ = false;
    home_access_ = home_access_t::hidden;
    project_dir_ = "";
    project_hash_ = "default";
    extra_rw_paths_.clear();
    extra_ro_paths_.clear();
}

void command_runner::apply_internal_profile() {
    apply_default_profile();
    bypass_sandbox_ = true;
}

void command_runner::apply_build_profile() {
    apply_default_profile();
    network_access_ = true;
    home_access_ = home_access_t::read_only;
    project_dir_ = get_repository_root();
    project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));
}

void command_runner::apply_strict_agent_profile() {
    apply_default_profile();
    network_access_ = false;
    home_access_ = home_access_t::hidden;
    project_dir_ = get_repository_root();
    project_hash_ = std::to_string(std::hash<std::string>{}(project_dir_));
}

std::string command_runner::build_command(const std::string& raw_command) const {
    if (bypass_sandbox_ && !config_manager::get_instance().is_paranoid_mode()) {
        return raw_command;
    }

    std::string random_suffix = std::to_string(rand() % 1000000);
    std::string unit_name = "turbostar-project-" + project_hash_ + "-" + random_suffix;

    std::string cmd = "systemd-run --user --pipe --wait --quiet ";
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

    for (const auto& p : extra_rw_paths_) {
        cmd += "-p ReadWritePaths=" + p + " ";
    }
    for (const auto& p : extra_ro_paths_) {
        if (home_access_ == home_access_t::hidden) {
            cmd += "-p BindReadOnlyPaths=" + p + " ";
        } else {
            cmd += "-p ReadOnlyPaths=" + p + " ";
        }
    }

    // Mask sensitive files natively at the kernel level
    std::vector<std::string> inaccessible_paths;
    
    const char* home_env = std::getenv("HOME");
    if (home_env) {
        std::string home(home_env);
        std::vector<std::string> sensitive = {
            home + "/.ssh",
            home + "/.env",
            home + "/.aws",
            home + "/.gnupg",
            home + "/.gemini/keys"
        };
        for (const auto& s : sensitive) {
            if (fs::exists(s)) {
                inaccessible_paths.push_back("-" + s);
            }
        }
    }

    if (!project_dir_.empty()) {
        std::vector<std::string> sensitive = {
            project_dir_ + "/.env",
            project_dir_ + "/.ssh"
        };
        for (const auto& s : sensitive) {
            if (fs::exists(s)) {
                inaccessible_paths.push_back("-" + s);
            }
        }
    }

    for (const auto& p : inaccessible_paths) {
        cmd += "-p InaccessiblePaths=" + p + " ";
    }

    // Escape single quotes in the raw command
    std::string escaped_command;
    for (char c : raw_command) {
        if (c == '\'') escaped_command += "'\\''";
        else escaped_command += c;
    }

    cmd += "-- bash -c '" + escaped_command + "'";
    return cmd;
}

int command_runner::execute(const std::string& command) {
    std::string final_command = build_command(command) + " 2>/dev/null";
    FILE* pipe = popen(final_command.c_str(), "r");
    
    if (!pipe) {
        return -1; // Failed to start
    }

    int fd = fileno(pipe);
    std::array<char, 256> buffer;
    std::string current_line;

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

        if (fgets(buffer.data(), buffer.size(), pipe) == nullptr) {
            break; // EOF
        }
        
        current_line += buffer.data();
        
        size_t pos;
        while ((pos = current_line.find('\n')) != std::string::npos) {
            std::string line = current_line.substr(0, pos);
            
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            on_output_line(line);
            current_line = current_line.substr(pos + 1);
        }
    }

    // Flush any remaining characters without a newline
    if (!current_line.empty()) {
        if (!current_line.empty() && current_line.back() == '\r') {
            current_line.pop_back();
        }
        on_output_line(current_line);
    }

    int exit_code = pclose(pipe);
    int status = WEXITSTATUS(exit_code);

    if (!bypass_coredump_check_ && !project_hash_.empty()) {
        last_coredumps_report_ = coredump_manager::get_instance().refresh(project_hash_);
    }

    return status;
}

std::string sync_command_runner::execute_and_get_output(const std::string& command) {
    full_output_.clear();
    exit_code_ = execute(command);
    return full_output_;
}

void sync_command_runner::on_output_line(const std::string& line) {
    full_output_ += line + "\n";
}