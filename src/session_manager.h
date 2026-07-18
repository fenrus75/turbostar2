#pragma once

#include <string>

class session_manager
{
      public:
	static session_manager &get_instance();

	void load();
	void save() const;

	std::string get_last_search_query() const { return last_search_query_; }
	void set_last_search_query(const std::string &q) { last_search_query_ = q; }
	std::string get_last_replace_query() const { return last_replace_query_; }
	void set_last_replace_query(const std::string &q) { last_replace_query_ = q; }

      private:
	session_manager() = default;
	~session_manager() = default;
	session_manager(const session_manager &) = delete;
	session_manager &operator=(const session_manager &) = delete;

	std::string get_session_file_path() const;

	std::string last_search_query_{""};
	std::string last_replace_query_{""};
};
