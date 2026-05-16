#include "git_manager.h"
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <array>
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
}

void git_manager::request_status(const std::string &filepath)
{
	if (filepath.empty() || filepath == "unknown.txt")
		return;

	std::unique_lock lock(queue_mutex_);
	pending_requests_.push(filepath);
	cv_.notify_one();
}

git_status git_manager::get_cached_status(const std::string &filepath) const
{
	std::unique_lock lock(cache_mutex_);
	auto it = status_cache_.find(filepath);
	if (it != status_cache_.end()) {
		return it->second;
	}
	return git_status::unknown;
}

void git_manager::worker_loop()
{
	while (!stop_thread_) {
		std::string filepath;
		{
			std::unique_lock lock(queue_mutex_);
			cv_.wait(lock, [this] { return !pending_requests_.empty() || stop_thread_; });
			if (stop_thread_)
				break;
			filepath = pending_requests_.front();
			pending_requests_.pop();
		}

		git_status status = run_git_status_cmd(filepath);

		bool changed = false;
		{
			std::unique_lock lock(cache_mutex_);
			if (status_cache_[filepath] != status) {
				status_cache_[filepath] = status;
				changed = true;
			}
		}

		if (changed && global_queue_) {
			editor_event ev;
			ev.type = event_type::git_status_updated;
			global_queue_->push(ev);
		}
	}
}

git_status git_manager::run_git_status_cmd(const std::string &filepath)
{
	if (!fs::exists(filepath))
		return git_status::unknown;

	fs::path p(filepath);
	std::string dir = p.parent_path().string();
	std::string file = p.filename().string();
	if (dir.empty())
		dir = ".";

	// Use -C to run git from the file's directory, so it correctly finds the repo.
	std::string cmd = "git -C " + dir + " status --porcelain -u " + file + " 2>/dev/null";
	std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

	if (!pipe) {
		return git_status::unknown;
	}

	std::array<char, 128> buffer;
	std::string result;
	while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
		result += buffer.data();
	}

	// If result is empty, it could be clean or not a repo.
	// But we redirected stderr, so we can't easily distinguish via output.
	// However, if it's clean and in a repo, exit code is 0.
	// If it's NOT a repo, exit code is 128.
	// popen/pclose return wait() status.
	
	// Wait, I can't easily get the exit code from popen on all platforms 
	// without WEXITSTATUS macro, which works on POSIX.
	
	// Let's check the result first.
	if (!result.empty()) {
		if (result.substr(0, 2) == "??") {
			return git_status::untracked;
		}
		// Any other porcelain prefix means dirty (M, A, D, R, C, U)
		return git_status::dirty;
	}

	// Result is empty. Check if we are in a git repo.
	std::string check_cmd = "git -C " + dir + " rev-parse --is-inside-work-tree 2>/dev/null";
	std::unique_ptr<FILE, decltype(&pclose)> check_pipe(popen(check_cmd.c_str(), "r"), pclose);
	std::array<char, 64> check_buffer;
	if (check_pipe && fgets(check_buffer.data(), check_buffer.size(), check_pipe.get()) != nullptr) {
		std::string check_res = check_buffer.data();
		if (check_res.find("true") != std::string::npos) {
			return git_status::clean;
		}
	}

	return git_status::unknown;
}
