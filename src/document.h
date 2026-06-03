#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include "event_queue.h"
#include "line.h"
#include "syntax_highlighter.h"

enum class undo_group_type {
	none,	     // Never merge (e.g., paste, load, formatting, agent edits, word deletes)
	typing,	     // Single-character insertion
	backspace,   // Single-character backspace/delete
	delete_line, // Whole-line deletes (Ctrl-Y)
};

// Represents a single, atomic line modification
struct edit_action {
	enum class action_type { replace_line, insert_line, delete_line };

	action_type type;
	int y;
	std::shared_ptr<line> saved_line;
};

struct action_group {
	std::string name;
	std::vector<edit_action> actions;
	int cursor_y_before{0};
	int cursor_x_before{0};
	int cursor_y_after{0};
	int cursor_x_after{0};
	undo_group_type type{undo_group_type::none};

	bool empty() const
	{
		return actions.empty();
	}
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
	virtual ~document();

	virtual bool load_from_file(const std::string &filename);
	bool insert_file(const std::string &filename);
	virtual bool save();
	virtual bool save_to_file(const std::string &filename);
	void clear();
	bool check_disk_changed();
	void update_last_disk_mtime();
	bool get_ignore_disk_changes() const;
	void set_ignore_disk_changes(bool ignore);
	const std::string &get_filename() const;
	const std::string &get_safe_filename() const;
	bool has_nondefault_filename() const;
	virtual bool is_modified() const;
	virtual void clear_modified();
	std::string get_git_branch() const;
	void set_git_branch(const std::string &branch);

	virtual bool is_read_only() const
	{
		return read_only_;
	}
	virtual void set_read_only(bool ro);

	// Basic accessors for now
	int line_count() const;
	size_t get_line_count() const;
	std::shared_ptr<line> get_line(int index) const;
	std::vector<std::string> get_all_lines() const;
	std::vector<diagnostic_info> get_diagnostics() const;

	int get_cursor_x() const;
	int get_cursor_y() const;
	std::string get_text_all() const;
	std::string get_word_under_cursor() const;
	void move_cursor(int dx, int dy);
	void request_redraw() const;
	void insert_char(const std::string &utf8_char);
	void insert_text(const std::string &text);
	void backspace();
	void delete_char();
	void delete_word_forward();
	void delete_word_backward();
	void delete_to_eol();
	void delete_to_bol();
	void split_line();
	void delete_line();

	void append_line(const std::string &text);
	void trim_top_lines(int max_lines);

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
	void set_selection(int start_y, int start_x, int end_y, int end_x);
	void clear_selection();
	void delete_selection();
	void copy_selection();
	void move_selection();
	bool has_selection() const;

	void get_selection_range(int &start_x, int &start_y, int &end_x, int &end_y) const;
	void get_selection_range_unlocked(int &start_x, int &start_y, int &end_x, int &end_y) const;
	void delete_selection_unlocked();

	void notify_cursor_changed() const;
	bool find_next(const search_params &params, bool is_repeat = false);

	void format_range(int start_y, int end_y);
	void format_paragraph();
	void trim_trailing_whitespace();

	std::optional<std::pair<int, int>> find_matching_bracket(int y, int x) const;
	void select_enclosing_scope();

	virtual void undo();
	virtual void redo();
	virtual void break_undo_coalescing();

	virtual size_t get_undo_count() const;
	std::vector<std::string> get_lines_at_undo(size_t steps_back) const;
	std::string get_undo_name(size_t steps_back) const;

	void apply_external_edits_json(const std::string &json_str);

	void set_lsp_highlights(const std::vector<text_range> &highlights)
	{
		std::unique_lock lock(mutex_);
		lsp_highlights_ = highlights;
	}
	const std::vector<text_range> &get_lsp_highlights() const
	{
		return lsp_highlights_;
	}

	void set_lsp_diagnostics(const std::vector<diagnostic_info> &diagnostics)
	{
		std::unique_lock lock(mutex_);
		lsp_diagnostics_ = diagnostics;
	}
	const std::vector<diagnostic_info> &get_lsp_diagnostics() const
	{
		return lsp_diagnostics_;
	}

	void set_enclosing_scope(const text_range &range)
	{
		std::unique_lock lock(mutex_);
		enclosing_scope_ = range;
	}
	const std::optional<text_range> &get_enclosing_scope() const
	{
		return enclosing_scope_;
	}

      protected:
	std::vector<line> get_selection_block() const;
	void insert_block(const std::vector<line> &block);
	void update_target_cursor_x_unlocked();
	void set_modified();
	int line_count_unlocked() const;
	void adjust_selection_for_insert(int y, int x, int count);
	void adjust_selection_for_delete(int y, int x, int count);
	void adjust_selection_for_split(int y, int x);
	void adjust_selection_for_join(int y, int x);
	void adjust_selection_for_line_delete(int y);

	// Syntax highlighting
	void mark_line_dirty(const std::shared_ptr<line> &l);
	void highlighter_thread_loop(std::stop_token stop_token);
	void process_line_highlight(std::shared_ptr<line> l);
	void refresh_highlighter();
	bool is_space_at(int y, int x) const;
	bool is_space_at_unlocked(int y, int x) const;
	std::string get_word_under_cursor_unlocked() const;

	std::vector<std::shared_ptr<line>> lines_;
	mutable std::shared_mutex mutex_;

	std::string filename_;
	std::string safe_filename_;
	std::string git_branch_;
	bool modified_{false};
	bool read_only_{false};
	std::filesystem::file_time_type last_disk_mtime_;
	bool has_last_disk_mtime_{false};
	bool ignore_disk_changes_{false};

	int cursor_x_{0};
	int cursor_y_{0};
	int target_cursor_x_{0}; // "Ghost X" for vertical navigation across short lines

	mutable std::string last_hover_word_;

	int selection_start_x_{-1};
	int selection_start_y_{-1};
	int selection_end_x_{-1};
	int selection_end_y_{-1};

	// Undo/Redo logic
	void begin_edit_group(const std::string &name = "", undo_group_type type = undo_group_type::none);
	void end_edit_group();
	void record_action(edit_action::action_type type, int y, std::shared_ptr<line> saved_line);
	void break_undo_coalescing_unlocked();
	void notify_undo_changed_event() const;

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
	std::jthread highlighter_thread_;
	std::shared_ptr<syntax_highlighter> active_highlighter_;

	mutable std::vector<text_range> lsp_highlights_;
	mutable std::vector<diagnostic_info> lsp_diagnostics_;
	mutable std::optional<text_range> enclosing_scope_;
};
