#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include "event_queue.h"
#include "line.h"

// Represents a single, atomic line modification
struct edit_action {
	enum class action_type {
		replace_line,
		insert_line,
		delete_line
	};

	action_type type;
	int y;
	std::shared_ptr<line> saved_line;
};

struct action_group {
	std::vector<edit_action> actions;
	int cursor_y_before{0};
	int cursor_x_before{0};
	int cursor_y_after{0};
	int cursor_x_after{0};

	bool empty() const { return actions.empty(); }
};

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

class document
{
      public:
	document(event_queue &global_queue);
	document(event_queue &global_queue, const std::string &filename);
	~document();

	bool load_from_file(const std::string &filename);
	void insert_file(const std::string &filename);
	bool save();
	bool save_to_file(const std::string &filename);
	void clear();
	const std::string &get_filename() const;
	bool has_nondefault_filename() const;
	bool is_modified() const;
	std::string get_git_branch() const;
	void set_git_branch(const std::string &branch);

	// Basic accessors for now
	int line_count() const;
	size_t get_line_count() const;
	std::shared_ptr<line> get_line(int index) const;

	int get_cursor_x() const;
	int get_cursor_y() const;
	void move_cursor(int dx, int dy);
	void insert_char(const std::string &utf8_char);
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

	void get_selection_range(int &start_x, int &start_y, int &end_x, int &end_y) const;

	void notify_cursor_changed() const;

	bool find_next(const search_params &params, bool is_repeat = false);

	void format_range(int start_y, int end_y);
	void format_paragraph();

	std::optional<std::pair<int, int>> find_matching_bracket(int y, int x) const;
	void select_enclosing_scope();

	void undo();
	void redo();

      private:
	std::vector<line> get_selection_block() const;
	void insert_block(const std::vector<line> &block);
	void set_modified();
	int line_count_unlocked() const;
	void adjust_selection_for_insert(int y, int x, int count);
	void adjust_selection_for_delete(int y, int x, int count);
	void adjust_selection_for_split(int y, int x);
	void adjust_selection_for_join(int y, int x);
	void adjust_selection_for_line_delete(int y);

	// Syntax highlighting
	void mark_line_dirty(std::shared_ptr<line> l);
	void highlighter_thread_loop();
	void process_line_highlight(std::shared_ptr<line> l);
	bool is_space_at(int y, int x) const;
	bool is_space_at_unlocked(int y, int x) const;

	std::vector<std::shared_ptr<line>> lines_;
	mutable std::shared_mutex mutex_;

	std::string filename_;
	std::string git_branch_;
	bool modified_{false};

	int cursor_x_{0};
	int cursor_y_{0};

	int selection_start_x_{-1};
	int selection_start_y_{-1};
	int selection_end_x_{-1};
	int selection_end_y_{-1};

	// Undo/Redo logic
	void begin_edit_group();
	void end_edit_group();
	void record_action(edit_action::action_type type, int y, std::shared_ptr<line> saved_line);

	std::deque<action_group> undo_stack_;
	std::deque<action_group> redo_stack_;
	action_group current_action_group_;
	bool is_recording_actions_{true};
	int edit_group_depth_{0};
	const size_t max_undo_steps_{1000};

	// Threading for highlighting
	event_queue &global_queue_;
	std::queue<std::shared_ptr<line>> dirty_lines_;
	std::mutex dirty_mutex_;
	std::condition_variable dirty_cv_;
	std::thread highlighter_thread_;
	std::atomic<bool> stop_thread_{false};
};
