#include "git_manager.h"
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <dtl/dtl.hpp>
#include <format>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include "command_runner.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "utf8.h"

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
	{
		std::unique_lock lock(cache_mutex_);
		cached_repo_root_.clear();
		repo_root_cached_ = false;
	}
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
std::shared_ptr<const git_info> git_manager::get_cached_info(const std::string &filepath) const
{
	std::unique_lock lock(cache_mutex_);
	auto it = status_cache_.find(filepath);
	if (it != status_cache_.end()) {
		return it->second;
	}
	return nullptr;
}

std::string git_manager::get_repository_root() const
{
	std::unique_lock lock(cache_mutex_);
	if (repo_root_cached_) {
		return cached_repo_root_;
	}
	lock.unlock();

	std::string cmd = "git rev-parse --show-toplevel 2>/dev/null";
	sync_command_runner runner;
	runner.apply_internal_profile();
	std::string result = runner.execute_and_get_output(cmd);
	if (!result.empty() && result.back() == '\n') {
		result.pop_back();
	}

	lock.lock();
	cached_repo_root_ = result;
	repo_root_cached_ = true;
	return cached_repo_root_;
}

void git_manager::worker_loop()
{
	fs_utils::set_current_thread_name("git_worker");
	event_logger::get_instance().log_info("Thread started: git_manager worker_loop");
        while (!stop_thread_ && !project_manager::get_instance().is_exiting()) {
                git_request req;
                {
                        std::unique_lock lock(queue_mutex_);
                        cv_.wait(lock, [this] { return !pending_requests_.empty() || stop_thread_ || project_manager::get_instance().is_exiting(); });
                        if (stop_thread_ || project_manager::get_instance().is_exiting())
                                break;
                        req = pending_requests_.front();
                        pending_requests_.pop();
                }

                if (req.type == request_type::status) {
                        git_info info = run_git_status_cmd(req.filepath);

                        bool changed = false;
                        {
                                std::unique_lock lock(cache_mutex_);
                                auto it = status_cache_.find(req.filepath);
                                const git_info* cached = it != status_cache_.end() ? it->second.get() : nullptr;
                                if (!cached || cached->status != info.status || cached->branch != info.branch || cached->diff != info.diff ||
                                    cached->has_head != info.has_head || cached->head_lines != info.head_lines) {
                                        if ((!cached || cached->branch != info.branch) && !info.branch.empty()) {
                                                event_logger::get_instance().log_info("Git: Detected branch '{}' for {}", info.branch, req.filepath);
                                        }
                                        status_cache_[req.filepath] = std::make_shared<const git_info>(std::move(info));
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
	event_logger::get_instance().log_info("Thread exited: git_manager worker_loop");
}

void git_manager::run_git_add_cmd(const std::string &filepath)
{
	fs::path p(filepath);
	std::string dir = p.parent_path().string();
	std::string file = p.filename().string();
	if (dir.empty()) {
		dir = ".";
	}

	std::string cmd = std::format("git -C {} add -- {} 2>/dev/null",
	                              fs_utils::escape_shell_arg(dir),
	                              fs_utils::escape_shell_arg(file));
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
	if (dir.empty()) {
		dir = ".";
	}

	// Single consolidated git invocation: gets branch, repo membership, and file status in one shot
	std::string cmd = std::format("git -C {} status --porcelain=v1 -b -u --ignored=matching -- {} 2>/dev/null",
	                              fs_utils::escape_shell_arg(dir),
	                              fs_utils::escape_shell_arg(file));
	sync_command_runner status_runner;
	status_runner.apply_internal_profile();
	std::string result = status_runner.execute_and_get_output(cmd);

	if (status_runner.get_exit_code() != 0) {
		info.status = git_status::unknown;
		info.diff.is_tracked = false;
		return info;
	}

	std::stringstream ss(result);
	std::string line;
	std::vector<std::string> lines;
	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if (!line.empty()) {
			lines.push_back(line);
		}
	}

	if (lines.empty()) {
		info.status = git_status::unknown;
		info.diff.is_tracked = false;
		return info;
	}

	// First line is branch info: "## <branch_spec>"
	bool has_head_commit = true;
	const std::string &header_line = lines[0];
	if (header_line.starts_with("## ")) {
		std::string_view branch_spec = std::string_view(header_line).substr(3);
		if (branch_spec.starts_with("No commits yet on ")) {
			info.branch = std::string(branch_spec.substr(18));
			has_head_commit = false;
		} else if (branch_spec.starts_with("HEAD (no branch)")) {
			info.branch = "HEAD";
		} else {
			size_t dots = branch_spec.find("...");
			if (dots != std::string_view::npos) {
				info.branch = std::string(branch_spec.substr(0, dots));
			} else {
				size_t sp = branch_spec.find(' ');
				if (sp != std::string_view::npos) {
					info.branch = std::string(branch_spec.substr(0, sp));
				} else {
					info.branch = std::string(branch_spec);
				}
			}
		}
	}

	if (lines.size() <= 1) {
		// Only header line present: file is tracked and unchanged
		info.status = git_status::clean;
	} else {
		// Status line format: XY filename (e.g. "??", "!!", " M", "M ", "A ")
		const std::string &status_line = lines[1];
		if (status_line.starts_with("??") || status_line.starts_with("!!")) {
			info.status = git_status::untracked;
		} else {
			info.status = git_status::dirty;
		}
	}

	if (info.status == git_status::dirty) {
		info.diff = run_git_diff_cmd(filepath);
		if (has_head_commit) {
			std::string show_cmd = std::format("git -C {} show HEAD:./{} 2>/dev/null",
			                                   fs_utils::escape_shell_arg(dir),
			                                   fs_utils::escape_shell_arg(file));
			sync_command_runner show_runner;
			show_runner.apply_internal_profile();
			std::string show_res = show_runner.execute_and_get_output(show_cmd);
			if (show_runner.get_exit_code() == 0) {
				info.has_head = true;
				info.head_lines.clear();
				std::stringstream head_ss(show_res);
				std::string l;
				while (std::getline(head_ss, l)) {
					if (!l.empty() && l.back() == '\r') {
						l.pop_back();
					}
					info.head_lines.push_back(l);
				}
			}
		}
	} else if (info.status == git_status::clean) {
		info.diff.is_tracked = true;
		info.diff.hunks.clear();
		if (has_head_commit) {
			// When clean, file on disk is identical to HEAD.
			// Read directly from disk without spawning a git subprocess!
			std::ifstream file_in(filepath);
			if (file_in.is_open()) {
				info.has_head = true;
				info.head_lines.clear();
				std::string l;
				while (std::getline(file_in, l)) {
					if (!l.empty() && l.back() == '\r') {
						l.pop_back();
					}
					info.head_lines.push_back(l);
				}
			}
		}
	} else {
		info.diff.is_tracked = false;
		info.diff.hunks.clear();
	}

	return info;
}

git_file_diff git_manager::run_git_diff_cmd(const std::string &filepath)
{
	git_file_diff diff;
	diff.is_tracked = true;

	fs::path p(filepath);
	std::string dir = p.parent_path().string();
	std::string file = p.filename().string();
	if (dir.empty()) {
		dir = ".";
	}

	std::string cmd = std::format("git -C {} diff -U0 HEAD -- {} 2>/dev/null",
	                              fs_utils::escape_shell_arg(dir),
	                              fs_utils::escape_shell_arg(file));
	sync_command_runner runner;
	runner.apply_internal_profile();
	std::string output = runner.execute_and_get_output(cmd);

	std::istringstream stream(output);
	std::string line;
	while (std::getline(stream, line)) {
		if (line.starts_with("@@ -")) {
			size_t end_hunk = line.find(" @@", 4);
			if (end_hunk != std::string::npos) {
				std::string_view hunk_header = std::string_view(line).substr(4, end_hunk - 4);
				size_t plus_pos = hunk_header.find(" +");
				if (plus_pos != std::string::npos) {
					std::string_view old_part = hunk_header.substr(0, plus_pos);
					std::string_view new_part = hunk_header.substr(plus_pos + 2);

					int old_start = 0, old_count = 1;
					size_t old_comma = old_part.find(',');
					if (old_comma != std::string::npos) {
						std::from_chars(old_part.data(), old_part.data() + old_comma, old_start);
						std::from_chars(old_part.data() + old_comma + 1, old_part.data() + old_part.size(), old_count);
					} else {
						std::from_chars(old_part.data(), old_part.data() + old_part.size(), old_start);
					}

					int new_start = 0, new_count = 1;
					size_t new_comma = new_part.find(',');
					if (new_comma != std::string::npos) {
						std::from_chars(new_part.data(), new_part.data() + new_comma, new_start);
						std::from_chars(new_part.data() + new_comma + 1, new_part.data() + new_part.size(), new_count);
					} else {
						std::from_chars(new_part.data(), new_part.data() + new_part.size(), new_start);
					}

					diff.hunks.push_back({old_start, old_count, new_start, new_count});
				}
			}
		}
	}
	return diff;
}

git_file_diff git_manager::compute_diff(const std::vector<std::string> &old_lines, const std::vector<std::string> &new_lines)
{
	git_file_diff diff;
	diff.is_tracked = true;

	if (old_lines.empty() && new_lines.empty()) {
		return diff;
	}

	dtl::Diff<std::string, std::vector<std::string>> d(old_lines, new_lines);
	d.compose();

	auto ses = d.getSes().getSequence();
	size_t i = 0;
	int last_common_old = 0;
	int last_common_new = 0;

	while (i < ses.size()) {
		if (ses[i].second.type == dtl::SES_COMMON) {
			last_common_old = static_cast<int>(ses[i].second.beforeIdx);
			last_common_new = static_cast<int>(ses[i].second.afterIdx);
			++i;
			continue;
		}

		int del_count = 0;
		int add_count = 0;
		int first_del_old = 0;
		int first_add_new = 0;

		while (i < ses.size() && ses[i].second.type != dtl::SES_COMMON) {
			if (ses[i].second.type == dtl::SES_DELETE) {
				if (del_count == 0) {
					first_del_old = static_cast<int>(ses[i].second.beforeIdx);
				}
				del_count++;
			} else if (ses[i].second.type == dtl::SES_ADD) {
				if (add_count == 0) {
					first_add_new = static_cast<int>(ses[i].second.afterIdx);
				}
				add_count++;
			}
			++i;
		}

		int old_start = (del_count > 0) ? first_del_old : last_common_old;
		int new_start = (add_count > 0) ? first_add_new : last_common_new;

		diff.hunks.push_back({old_start, del_count, new_start, add_count});
	}

	return diff;
}

git_file_diff::line_info git_file_diff::get_line_info(int buffer_line_1based) const
{
	line_info info;
	if (hunks.empty() || buffer_line_1based < 1) {
		info.git_line = buffer_line_1based;
		return info;
	}

	int cumulative_delta = 0;
	for (const auto &h : hunks) {
		if (h.new_count == 0) {
			if (buffer_line_1based <= h.new_start) {
				info.git_line = buffer_line_1based - cumulative_delta;
				return info;
			}
			if (buffer_line_1based == h.new_start + 1) {
				info.has_deletion_above = true;
				info.deleted_count += h.old_count;
			}
			cumulative_delta += (h.new_count - h.old_count);
			continue;
		}

		int h_start = h.new_start;
		int h_end = h.new_start + h.new_count - 1;

		if (buffer_line_1based < h_start) {
			info.git_line = buffer_line_1based - cumulative_delta;
			return info;
		}

		if (buffer_line_1based <= h_end) {
			int offset = buffer_line_1based - h_start;
			int mod_count = std::min(h.old_count, h.new_count);
			if (offset < mod_count) {
				info.type = git_line_diff_type::modified;
				info.git_line = h.old_start + offset;
			} else {
				info.type = git_line_diff_type::added;
				info.git_line = -1;
			}
			return info;
		}

		if (h.old_count > h.new_count && buffer_line_1based == h_end + 1) {
			info.has_deletion_above = true;
			info.deleted_count += (h.old_count - h.new_count);
		}

		cumulative_delta += (h.new_count - h.old_count);
	}

	info.git_line = buffer_line_1based - cumulative_delta;
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
		if (!utf8::is_alnum(c, "/._-@~^{}:+")) {
			return false;
		}
	}
	return true;
}

std::string git_manager::get_remote_origin_url() const
{
	std::string root = get_repository_root();
	if (root.empty()) {
		return "";
	}
	std::string url = fs_utils::execute_command_sync(
	    std::format("git -C {} config --get remote.origin.url", fs_utils::escape_shell_arg(root)));
	size_t pos = url.find('\n');
	if (pos != std::string::npos) {
		url = url.substr(0, pos);
	}
	size_t start = url.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = url.find_last_not_of(" \t\r\n");
	return url.substr(start, end - start + 1);
}

std::string git_manager::get_current_branch() const
{
	std::string root = get_repository_root();
	if (root.empty()) {
		return "";
	}
	std::string branch = fs_utils::execute_command_sync(
	    std::format("git -C {} rev-parse --abbrev-ref HEAD", fs_utils::escape_shell_arg(root)));
	size_t pos = branch.find('\n');
	if (pos != std::string::npos) {
		branch = branch.substr(0, pos);
	}
	size_t start = branch.find_first_not_of(" \t\r\n");
	if (start == std::string::npos) return "";
	size_t end = branch.find_last_not_of(" \t\r\n");
	std::string res = branch.substr(start, end - start + 1);
	return (res == "HEAD") ? "" : res;
}


