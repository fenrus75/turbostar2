#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <optional>
#include "event_queue.h"

/**
 * @brief Manages the list of errors parsed from build logs.
 */
class build_error_manager
{
      public:
	static build_error_manager &get_instance();

	void clear();
	void add_error(const build_error &err);
	
	std::vector<build_error> get_errors() const;

	std::optional<build_error> get_next_error();
	void reset_navigation();

	std::optional<build_error> find_error_at(const std::string& filepath, int line) const;

	std::time_t get_last_compile_time() const;

	private:
	build_error_manager() = default;

	std::vector<build_error> errors_;
	int current_index_{-1};
	/*
	 * mutex_ protects the errors_ list, navigation current_index_, and last_compile_time_.
	 * Locking Rules:
	 * - Held briefly when clearing, adding, or querying errors, navigating through errors,
	 *   or updating the compilation timestamp.
	 */
	mutable std::mutex mutex_;
	std::time_t last_compile_time_{0};
};
