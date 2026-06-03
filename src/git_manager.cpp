#include "git_manager.h"
#include <array>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <format>
#include <filesystem>
#include <iostream>
#include <memory>
#include "command_runner.h"
#include "event_logger.h"

namespace fs = std::filesystem;

git_manager &git_manager::get_instance()
{
	static git_manager instance;
	return instance;
}

git_manager::~git_manager()
{
	stop();
}

void git_manager::start(event_queue &queue)
{
	if (worker_thread_.joinable()) {
		event_logger::get_instance().log("Warning: git_manager::start called on an already running instance");
		assert(!worker_thread_.joinable() && "git_manager started twice!");
		return;
	}
	global_queue_ = &queue;
	stop_thread_ = false;
	worker_thread_ = std::thread(&git_manager::worker_loop, this);
}

void git_manager::stop()
{
        stop_thread_ = true;
        cv_.notify_all();
        if (worker_thread_.joinable()) {
                worker_thread_.join();
        }
        global_queue_.store(nullptr);
}
void git_manager::request_status(const std::string &filepath)
{
        if (filepath.empty() || filepath == "unknown.txt")
                return;

        std::unique_lock lock(queue_mutex_);
        if (pending_requests_.size() >= 1000) {
            return;
        }
        pending_requests_.push({request_type::status, filepath});
        cv_.notify_one();
}

void git_manager::git_add(const std::string &filepath)
{
        if (filepath.empty() || filepath == "unknown.txt")
                return;

        std::unique_lock lock(queue_mutex_);
        if (pending_requests_.size() >= 1000) {
            return;
        }
        pending_requests_.push({request_type::add, filepath});
        cv_.notify_one();
}
git_info git_manager::get_cached_info(const std::string &filepath) const
{
	std::unique_lock lock(cache_mutex_);
	auto it = status_cache_.find(filepath);
	if (it != status_cache_.end()) {
		return it->second;
	}
	return {};
}

std::string git_manager::get_repository_root() const
{
	std::string cmd = "git rev-parse --show-toplevel 2>/dev/null";
	sync_command_runner runner;
	runner.apply_internal_profile();
	std::string result = runner.execute_and_get_output(cmd);
	if (!result.empty() && result.back() == '\n') {
		result.pop_back();
	}
	return result;
}

void git_manager::worker_loop()
{
	event_logger::get_instance().log("Thread started: git_manager worker_loop");
        while (!stop_thread_) {
                git_request req;
                {
                        std::unique_lock lock(queue_mutex_);
                        cv_.wait(lock, [this] { return !pending_requests_.empty() || stop_thread_; });
                        if (stop_thread_)
                                break;
                        req = pending_requests_.front();
                        pending_requests_.pop();
                }

                if (req.type == request_type::status) {
                        git_info info = run_git_status_cmd(req.filepath);

                        bool changed = false;
                        {
                                std::unique_lock lock(cache_mutex_);
                                git_info& cached = status_cache_[req.filepath];
                                if (cached.status != info.status || cached.branch != info.branch) {
                                        if (cached.branch != info.branch && !info.branch.empty()) {
                                                event_logger::get_instance().log("Git: Detected branch '{}' for {}", info.branch, req.filepath);
                                        }
                                        cached = info;
                                        changed = true;
                                }
                        }

                        event_queue* queue = global_queue_.load();
                        if (changed && queue) {
                                editor_event ev;
                                ev.type = event_type::git_status_updated;
                                queue->push(ev);
                        }               } else if (req.type == request_type::add) {
                        run_git_add_cmd(req.filepath);
                        // After add, refresh status
                        request_status(req.filepath);
                }
        }
	event_logger::get_instance().log("Thread exited: git_manager worker_loop");
}

void git_manager::run_git_add_cmd(const std::string &filepath)
{
	fs::path p(filepath);
	std::string dir = p.parent_path().string();
	std::string file = p.filename().string();
	if (dir.empty())
		dir = ".";

	std::string cmd = "git -C " + dir + " add " + file + " 2>/dev/null";
	sync_command_runner runner;
	runner.apply_internal_profile();
	runner.execute_and_get_output(cmd);
}

git_info git_manager::run_git_status_cmd(const std::string &filepath)
{
        git_info info;

        if (!fs::exists(filepath)) {
                std::unique_lock lock(cache_mutex_);
                status_cache_.erase(filepath);
                return info;
        }

        fs::path p(filepath);
        std::string dir = p.parent_path().string();
        std::string file = p.filename().string();
        if (dir.empty())
                dir = ".";

        // Fetch branch
        std::string branch_cmd = "git -C " + dir + " rev-parse --abbrev-ref HEAD 2>/dev/null";
        sync_command_runner branch_runner;
        branch_runner.apply_internal_profile();
        std::string branch_res = branch_runner.execute_and_get_output(branch_cmd);
        if (!branch_res.empty()) {
                info.branch = branch_res;
                if (!info.branch.empty() && info.branch.back() == '\n')
                        info.branch.pop_back();
        }
	// Use -C to run git from the file's directory, so it correctly finds the repo.
	std::string cmd = "git -C " + dir + " status --porcelain -u " + file + " 2>/dev/null";
	sync_command_runner status_runner;
	status_runner.apply_internal_profile();
	std::string result = status_runner.execute_and_get_output(cmd);

	if (!result.empty()) {
		if (result.substr(0, 2) == "??") {
			info.status = git_status::untracked;
		} else {
			info.status = git_status::dirty;
		}
	} else {
		// Result is empty. Check if we are in a git repo.
		std::string check_cmd = "git -C " + dir + " rev-parse --is-inside-work-tree 2>/dev/null";
		sync_command_runner check_runner;
		check_runner.apply_internal_profile();
		std::string check_res = check_runner.execute_and_get_output(check_cmd);
		if (check_res.find("true") != std::string::npos) {
			info.status = git_status::clean;
		}
	}

	return info;
}

bool git_manager::is_valid_branch_name(const std::string &name)
{
	if (name.empty()) {
		return false;
	}
	// Prevent flag injection
	if (name.front() == '-') {
		return false;
	}
	// Prevent directory traversal or ref range
	if (name.find("..") != std::string::npos) {
		return false;
	}
	// Prevent lock files
	if (name.ends_with(".lock")) {
		return false;
	}
	// Prevent @{
	if (name.find("@{") != std::string::npos) {
		return false;
	}
	// Prevent double slashes or start/end with slash
	if (name.front() == '/' || name.back() == '/' || name.find("//") != std::string::npos) {
		return false;
	}
	// Prevent end with a dot
	if (name.back() == '.') {
		return false;
	}

	// Split by '/' to ensure no component starts with a dot
	size_t pos = 0;
	while (pos < name.length()) {
		if (pos == 0 || name[pos - 1] == '/') {
			if (name[pos] == '.') {
				return false;
			}
		}
		pos++;
	}

	// Loop through characters to reject forbidden chars
	for (char c : name) {
		// ASCII control chars under 32 and DEL (127) are blocked
		if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 127) {
			return false;
		}
		// Characters forbidden: space, ~, ^, :, ?, *, [, backslash
		// Also quotes, pipes, redirection etc. for safety
		bool is_forbidden = (c == ' ' || c == '~' || c == '^' || c == ':' || c == '?' || c == '*' || c == '[' || c == '\\' ||
				     c == '\'' || c == '"' || c == '`' || c == '$' || c == ';' || c == '&' || c == '|' || c == '<' || c == '>');
		if (is_forbidden) {
			return false;
		}
	}
	return true;
}

bool git_manager::is_valid_revision(const std::string &revision)
{
	if (revision.empty()) {
		return false;
	}
	// Prevent flag injection
	if (revision.front() == '-') {
		return false;
	}
	// Prevent directory traversal or ref range
	if (revision.find("..") != std::string::npos) {
		return false;
	}

	// Loop through characters
	for (char c : revision) {
		// ASCII control chars under 32 and DEL (127) are blocked
		if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) == 127) {
			return false;
		}
		// List of allowed characters for revisions:
		// Alphanumeric, slash, dot, underscore, hyphen, @, ~, ^, {, }, :, +
		// Block everything else (including space, quotes, shell operators)
		bool is_allowed = std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '.' || c == '_' || c == '-' ||
				  c == '@' || c == '~' || c == '^' || c == '{' || c == '}' || c == ':' || c == '+';
		if (!is_allowed) {
			return false;
		}
	}
	return true;
}


