#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include "line.h"
#include "event_queue.h"
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>

/**
 * @brief Parameters for document search operations.
 */
struct search_params {
	std::string query;
	std::string replacement;
	bool ignore_case{true};
	bool whole_words{false};
	bool regex{false};
	bool prompt_on_replace{true};
	bool backward{false};
	bool selected_text_only{false};
	bool from_cursor{true};
};


class document {
public:
	document(event_queue& global_queue);
	document(event_queue& global_queue, const std::string& filename);
	~document();

	bool load_from_file(const std::string& filename);
	bool save();
	bool save_to_file(const std::string& filename);
	void clear();
	const std::string& get_filename() const;
	bool is_modified() const;
	
	// Basic accessors for now
	size_t get_line_count() const;
	std::shared_ptr<line> get_line(size_t index) const;

	int get_cursor_x() const;
	int get_cursor_y() const;
	void move_cursor(int dx, int dy);
	void insert_char(const std::string& utf8_char);
	void backspace();
	void delete_char();
	void delete_word_forward();
	void delete_word_backward();
	void delete_to_eol();
	void delete_to_bol();
	void split_line();
	void delete_line();

	void move_to_bol();
	void move_to_eol();
	void move_to_top();
	void move_to_bottom();
	void move_page_up(int page_height);
	void move_page_down(int page_height);
	void move_next_word();
	void move_prev_word();

	// Selection management
	void set_selection_start();
	void set_selection_end();
	void clear_selection();
	void delete_selection();
	void copy_selection();
	void move_selection();
	bool has_selection() const;

	void get_selection_range(int& start_x, int& start_y, int& end_x, int& end_y) const;

	void log_state() const;

	bool find_next(const search_params& params);

private:
	std::vector<line> get_selection_block() const;
	void insert_block(const std::vector<line>& block);
	void set_modified();
	void adjust_selection_for_insert(int y, int x, int count);
	void adjust_selection_for_delete(int y, int x, int count);
	void adjust_selection_for_split(int y, int x);
	void adjust_selection_for_join(int y, int x);
	void adjust_selection_for_line_delete(int y);

	// Syntax highlighting
	void mark_line_dirty(std::shared_ptr<line> l);
	void highlighter_thread_loop();
	void process_line_highlight(std::shared_ptr<line> l);

	std::vector<std::shared_ptr<line>> lines_;
	mutable std::shared_mutex mutex_;
	
	std::string filename_;
	bool modified_{false};
	
	int cursor_x_{0};
	int cursor_y_{0};
	
	int selection_start_x_{-1};
	int selection_start_y_{-1};
	int selection_end_x_{-1};
	int selection_end_y_{-1};

	// Threading for highlighting
	event_queue& global_queue_;
	std::queue<std::shared_ptr<line>> dirty_lines_;
	std::mutex dirty_mutex_;
	std::condition_variable dirty_cv_;
	std::thread highlighter_thread_;
	std::atomic<bool> stop_thread_{false};
};
