#include "build_error_manager.h"
#include <algorithm>
#include <filesystem>
#include "fs_utils.h"

namespace fs = std::filesystem;

build_error_manager &build_error_manager::get_instance()
{
	static build_error_manager instance;
	return instance;
}

void build_error_manager::clear()
{
	std::lock_guard lock(mutex_);
	errors_.clear();
	current_index_ = -1;
}

void build_error_manager::add_error(const build_error &err)
{
	std::lock_guard lock(mutex_);
	errors_.push_back(err);
}

const std::vector<build_error>& build_error_manager::get_errors() const
{
	std::lock_guard lock(mutex_);
	return errors_;
}

std::optional<build_error> build_error_manager::get_next_error()
{
	std::lock_guard lock(mutex_);
	if (errors_.empty()) return std::nullopt;
	
	current_index_ = (current_index_ + 1) % errors_.size();
	return errors_[current_index_];
}

void build_error_manager::reset_navigation()
{
	std::lock_guard lock(mutex_);
	current_index_ = -1;
}

std::optional<build_error> build_error_manager::find_error_at(const std::string& filepath, int line) const
{
	std::lock_guard lock(mutex_);
	if (filepath.empty()) return std::nullopt;

	std::string abs_path = fs_utils::safe_absolute(filepath).lexically_normal().string();
	
	for (const auto& err : errors_) {
		if (err.line == line) {
			if (!err.filepath.empty() && fs_utils::safe_absolute(err.filepath).lexically_normal().string() == abs_path) {
				return err;
			}
			if (err.filepath == filepath) return err;
		}
	}
	return std::nullopt;
}
