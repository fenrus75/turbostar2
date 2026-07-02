#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class input_history_manager
{
      public:
	static input_history_manager &get_instance();

	void load();
	void save();

	void add_entry(const std::string &history_id, const std::string &entry);
	const std::vector<std::string> &get_history(const std::string &history_id);

      private:
	input_history_manager() = default;
	~input_history_manager() = default;
	input_history_manager(const input_history_manager &) = delete;
	input_history_manager &operator=(const input_history_manager &) = delete;

	std::unordered_map<std::string, std::vector<std::string>> histories_;
	std::string get_history_file_path() const;
};
