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
	
	const std::vector<build_error>& get_errors() const;
	
	std::optional<build_error> get_next_error();
	void reset_navigation();

	std::optional<build_error> find_error_at(const std::string& filepath, int line) const;

      private:
	build_error_manager() = default;
	
	std::vector<build_error> errors_;
	int current_index_{-1};
	mutable std::mutex mutex_;
};
