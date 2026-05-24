#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

struct cursor_pos {
	int x{0};
	int y{0};
};

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

	void set_cursor_pos(const std::string &filename, int x, int y);
	std::optional<cursor_pos> get_cursor_pos(const std::string &filename) const;

	void set_project_files(const std::string &project_root, const std::vector<std::string> &files);
	std::vector<std::string> get_project_files(const std::string &project_root) const;

      private:
	history_manager() = default;
	~history_manager() = default;
	history_manager(const history_manager &) = delete;
	history_manager &operator=(const history_manager &) = delete;

	std::string get_history_file_path() const;

	std::deque<std::string> searches_;
	std::deque<std::string> files_;
	std::unordered_map<std::string, cursor_pos> cursor_memory_;
	std::deque<std::string> cursor_lru_;
	std::unordered_map<std::string, std::vector<std::string>> project_files_;
	
	const size_t max_history_items_ = 50;
	const size_t max_cursor_memory_ = 25;
};
