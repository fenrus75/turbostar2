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
#include "highlighter/syntax_highlighter.h"
/*

# subclasses of document_listener

| subclass           | filename                     |
| ------------------ | ---------------------------- |
| codereview_manager | src/codereview_manager.h    |
| perf_manager       | src/perf_manager.h           |

*/
class document_listener {
public:
	virtual ~document_listener() = default;
	virtual void on_line_inserted(const std::string &filename, int y) = 0;
	virtual void on_line_deleted(const std::string &filename, int y) = 0;
	virtual void on_document_changed(const std::string &/*filename*/) {}
};

enum class undo_group_type {
	none,	     // Never merge (e.g., paste, load, formatting, agent edits, word deletes)
	typing,	     // Single-character insertion
	backspace,   // Single-character backspace/delete
	delete_line, // Whole-line deletes (Ctrl-Y)
};

struct cursor_state {
	int cursor_x{0};
	int cursor_y{0};
	int target_cursor_x{0};
	int selection_start_x{-1};
	int selection_start_y{-1};
	int selection_end_x{-1};
	int selection_end_y{-1};
	std::string current_line_text;
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
	document(event_queue &global_queue, std::string_view filename);
	virtual ~document();

	void add_listener(document_listener *l);
	void remove_listener(document_listener *l);

	virtual bool load_from_file(const std::string &filename);
	bool insert_file(std::string_view filename);
	virtual bool save();
	virtual bool save_to_file(const std::string &filename);
	void clear();
	bool check_disk_changed();
	void update_last_disk_mtime();
	bool get_ignore_disk_changes() const noexcept;
	void set_ignore_disk_changes(bool ignore);
	const std::string &get_filename() const noexcept;
	const std::string &get_safe_filename() const noexcept;
	bool has_nondefault_filename() const noexcept;
	virtual bool is_modified() const;
	virtual void clear_modified();
	std::string get_git_branch() const;
	void set_git_branch(std::string_view branch);

	virtual bool is_read_only() const noexcept
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
	cursor_state capture_cursor_state() const;
	int restore_cursor_state(const cursor_state &state);
	std::string get_text_all() const;
	std::string get_word_under_cursor() const;
	void move_cursor(int dx, int dy);
	void set_cursor_position(int col, int line);
	void request_redraw() const;
	void insert_char(std::string_view utf8_char);
	void insert_text(std::string_view text);
	void backspace();
	void delete_char();
	void delete_word_forward();
	void delete_word_backward();
	void delete_to_eol();
	void delete_to_bol();
	void split_line();
	void delete_line();

	void append_line(std::string_view text);
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
	bool has_selection() const noexcept;
	bool write_selection_to_file(std::string_view filename);

	void get_selection_range(int &start_x, int &start_y, int &end_x, int &end_y) const;
	void get_selection_range_unlocked(int &start_x, int &start_y, int &end_x, int &end_y) const;
	void delete_selection_unlocked();

	void notify_cursor_changed() const;
	bool find_next(const search_params &params, bool is_repeat = false);
	bool replace_current(const search_params &params);
	int replace_all(const search_params &params);

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

	void apply_external_edits_json(std::string_view json_str);

	void set_lsp_highlights(std::span<const text_range> highlights)
	{
		std::unique_lock lock(mutex_);
		lsp_highlights_.assign(highlights.begin(), highlights.end());
	}
	std::vector<text_range> get_lsp_highlights() const
	{
		std::shared_lock lock(mutex_);
		return lsp_highlights_;
	}

	void set_lsp_diagnostics(std::span<const diagnostic_info> diagnostics)
	{
		std::unique_lock lock(mutex_);
		lsp_diagnostics_.assign(diagnostics.begin(), diagnostics.end());
	}
	std::vector<diagnostic_info> get_lsp_diagnostics() const
	{
		std::shared_lock lock(mutex_);
		return lsp_diagnostics_;
	}

	void set_enclosing_scope(const text_range &range)
	{
		std::unique_lock lock(mutex_);
		enclosing_scope_ = range;
	}
	std::optional<text_range> get_enclosing_scope() const
	{
		std::shared_lock lock(mutex_);
		return enclosing_scope_;
	}

      protected:
	std::vector<line> get_selection_block() const;
	void insert_block(std::span<const line> block, bool whole_lines = false);
	void update_target_cursor_x_unlocked();
	cursor_state capture_cursor_state_unlocked() const;
	int restore_cursor_state_unlocked(const cursor_state &state);
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
	bool is_terminator_at_unlocked(int y, int x) const;
	std::string get_word_under_cursor_unlocked() const;

	std::vector<std::shared_ptr<line>> lines_;

	/*
	 * mutex_ is a shared reader-writer mutex protecting the document content (lines_),
	 * cursor coordinates (cursor_x_, cursor_y_, target_cursor_x_), file metadata/state
	 * (filename_, modified_, read_only_, last_disk_mtime_), selection boundaries,
	 * and the undo/redo stacks.
	 * Locking Rules:
	 * - Shared locks (readers) are used for get_line(), line_count(), get_all_lines(), etc.
	 * - Exclusive locks (writers) are used for load, save, insert, delete, cursor movement,
	 *   selection modifications, and undo/redo operations.
	 */
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
	int last_match_len_chars_{0};

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

	/*
	 * dirty_mutex_ protects the dirty_lines_ queue of lines requiring syntax highlighting.
	 * Locking Rules:
	 * - Held briefly when marking a line as dirty and adding it to the queue, or when
	 *   popping lines from the queue inside the background highlighter thread.
	 * - Used in conjunction with dirty_cv_ to wake up the highlighter thread.
	 */
	std::mutex dirty_mutex_;
	std::condition_variable dirty_cv_;
	std::jthread highlighter_thread_;
	std::shared_ptr<syntax_highlighter> active_highlighter_;

	mutable std::vector<text_range> lsp_highlights_;
	mutable std::vector<diagnostic_info> lsp_diagnostics_;
	mutable std::optional<text_range> enclosing_scope_;
	std::vector<document_listener*> listeners_;
};
