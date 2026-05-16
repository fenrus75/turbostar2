#pragma once

#include <deque>
#include <string>

class history_manager
{
      public:
	static history_manager &get_instance();

	void load();
	void save() const;

	void add_search(const std::string &search_string);
	void add_file(const std::string &filename);

	const std::deque<std::string> &get_searches() const { return searches_; }
	const std::deque<std::string> &get_files() const { return files_; }

      private:
	history_manager() = default;
	~history_manager() = default;
	history_manager(const history_manager &) = delete;
	history_manager &operator=(const history_manager &) = delete;

	std::string get_history_file_path() const;

	std::deque<std::string> searches_;
	std::deque<std::string> files_;
	const size_t max_history_items_ = 50;
};
