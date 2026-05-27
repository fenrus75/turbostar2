#include "build_error_manager.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include "fs_utils.h"

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
        last_compile_time_ = std::time(nullptr);
}

void build_error_manager::add_error(const build_error &err)
{
        std::lock_guard lock(mutex_);
        build_error normalized_err = err;
        if (!normalized_err.filepath.empty()) {
                normalized_err.filepath = fs_utils::safe_absolute(normalized_err.filepath).string();
        }

        constexpr size_t MAX_ERRORS = 10000;
        if (errors_.size() >= MAX_ERRORS) {
            errors_.erase(errors_.begin());
        }
        errors_.push_back(normalized_err);
}

std::vector<build_error> build_error_manager::get_errors() const
{
        std::lock_guard lock(mutex_);
        return errors_;
}

std::time_t build_error_manager::get_last_compile_time() const
{
        std::lock_guard lock(mutex_);
        return last_compile_time_;
}

std::optional<build_error> build_error_manager::get_next_error()
{
        std::lock_guard lock(mutex_);
        if (errors_.empty())
                return std::nullopt;

        current_index_++;
        if (current_index_ >= static_cast<int>(errors_.size())) {
                current_index_ = 0;
        }
        return errors_[current_index_];
}

void build_error_manager::reset_navigation()
{
        std::lock_guard lock(mutex_);
        current_index_ = -1;
}

std::optional<build_error> build_error_manager::find_error_at(const std::string &filepath, int line) const
{
        if (filepath.empty())
                return std::nullopt;

        std::lock_guard lock(mutex_);
        if (errors_.empty())
                return std::nullopt;

        for (const auto &err : errors_) {
                if (err.line == line) {
                        if (err.filepath == filepath) {
                                return err;
                        }
                }
        }
        return std::nullopt;
}
