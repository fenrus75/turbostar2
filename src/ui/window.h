#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "document.h"
#include "event_queue.h"
#include "git_manager.h"

/*

# subclasses of window

| subclass           | filename                                  |
| ------------------ | ----------------------------------------- |
| agent_window       | src/ui/agent_window.h                    |
| agent_center_window | src/ui/agent_center_window.h              |
| code_review_window | src/ui/code_review_window.h              |
| crashdump_window   | src/ui/crashdump_window.h                |
| diff_window        | src/ui/diff_window.h                     |
| hex_editor_window  | src/ui/hex_editor_window.h               |
| terminal_window    | src/ui/terminal_window.h                  |

*/
enum class gutter_number_mode {
	git,
	file,
	none
};

class window
{
      public:
	window(int id, int x, int y, int width, int height, const std::string &title);
	virtual ~window();

	bool draw(bool cursor_only = false) const;
	void set_active(bool active);
	bool is_active() const;

	void attach_document(std::shared_ptr<document> doc);
	/**
	 * @brief Returns the window's local event queue.
	 */
	event_queue &get_queue();

	uint64_t get_last_active_timestamp() const
	{
		return last_active_timestamp_;
	}
	void update_last_active_timestamp();

	virtual int get_display_priority() const;
	void set_display_priority(int priority)
	{
		display_priority_ = priority;
	}
	void link_window(window *other);
	void unlink_window(window *other);
	const std::vector<window *> &get_linked_windows() const { return linked_windows_; }

	bool is_visible() const
	{
		return is_visible_;
	}
	void set_visible(bool visible)
	{
		is_visible_ = visible;
	}

	bool bypass_global_shortcuts() const
	{
		return bypass_global_shortcuts_;
	}
	void set_bypass_global_shortcuts(bool bypass)
	{
		bypass_global_shortcuts_ = bypass;
	}

	int get_x() const
	{
		return x_;
	}
	int get_y() const
	{
		return y_;
	}
	int get_width() const
	{
		return width_;
	}
	int get_height() const
	{
		return height_;
	}
	int get_id() const
	{
		return id_;
	}
	void set_bounds(int x, int y, int width, int height);
	bool is_maximized() const
	{
		return is_maximized_;
	}
	void set_maximized(bool max)
	{
		is_maximized_ = max;
	}
	int get_restore_x() const
	{
		return restore_x_;
	}
	int get_restore_y() const
	{
		return restore_y_;
	}
	int get_restore_width() const
	{
		return restore_width_;
	}
	int get_restore_height() const
	{
		return restore_height_;
	}
	void save_restore_bounds()
	{
		restore_x_ = x_;
		restore_y_ = y_;
		restore_width_ = width_;
		restore_height_ = height_;
	}
	int get_popup_button_x() const
	{
		return x_ + width_ - 10;
	}
	int get_git_button_width() const;

	gutter_number_mode get_gutter_number_mode() const noexcept
	{
		return gutter_mode_;
	}
	void set_gutter_number_mode(gutter_number_mode mode) noexcept
	{
		gutter_mode_ = mode;
	}
	void cycle_gutter_number_mode() noexcept
	{
		if (gutter_mode_ == gutter_number_mode::git) {
			gutter_mode_ = gutter_number_mode::file;
		} else if (gutter_mode_ == gutter_number_mode::file) {
			gutter_mode_ = gutter_number_mode::none;
		} else {
			gutter_mode_ = gutter_number_mode::git;
		}
	}

	int get_gutter_button_x() const;
	int get_gutter_button_width() const;
	git_file_diff get_effective_diff() const;

	int get_background_color_pair() const
	{
		return background_color_pair_;
	}
	void set_background_color_pair(int pair)
	{
		background_color_pair_ = pair;
	}

	/**
	 * @brief Processes all pending events in the window's local queue.
	 * @return true if an event was processed that requires a re-render.
	 */
	virtual bool process_events();
	void invalidate();
	void invalidate_cursor();
	bool needs_cursor() const;
	void clear_needs_cursor();
	virtual void set_cursor_position() const;
	virtual bool is_cursor_visible() const
	{
		return true;
	}
	int get_cursor_x() const;
	int get_cursor_y() const;

	std::string get_title() const
	{
		return title_;
	}
	virtual std::string get_displayed_title() const;
	virtual std::string get_status_help() const
	{
		return "";
	}
	void set_title(const std::string &t)
	{
		title_ = t;
	}
	std::shared_ptr<document> get_document() const
	{
		return doc_;
	}

	int get_content_height() const
	{
		return height_ - 2;
	}

	virtual int get_scroll_total_lines() const;
	virtual int get_scroll_top_line() const;
	virtual int get_scroll_content_height() const;

	event_queue &get_window_queue()
	{
		return window_queue_;
	}

	bool has_mouse_selection() const noexcept
	{
		return mouse_sel_start_line_ != -1 && mouse_sel_end_line_ != -1 &&
		       (mouse_sel_start_line_ != mouse_sel_end_line_ || mouse_sel_start_char_ != mouse_sel_end_char_);
	}
	virtual std::string get_mouse_selected_text() const;

      protected:
	int x_, y_, width_, height_;
	int restore_x_{0}, restore_y_{0}, restore_width_{0}, restore_height_{0};
	bool is_maximized_{false};
	int id_;
	int background_color_pair_{3};
	virtual void draw_content(bool cursor_only = false) const;
	virtual void draw_border() const;
	virtual void on_resize(int /*width*/, int /*height*/)
	{
	}
	virtual void on_move(int /*x*/, int /*y*/)
	{
	}
	virtual bool update_viewport() const;

      private:

	std::string title_;
	bool is_active_{false};

	uint64_t last_active_timestamp_{0};
	int display_priority_{0};
	bool is_visible_{true};
	bool bypass_global_shortcuts_{false};
	gutter_number_mode gutter_mode_{gutter_number_mode::git};
	mutable git_file_diff live_diff_;
	mutable uint64_t last_diff_revision_{static_cast<uint64_t>(-1)};
	mutable bool live_diff_valid_{false};

	mutable int top_line_{0};
	mutable int left_column_{0};

	bool needs_render_{false};
	bool needs_cursor_{false};
	mutable std::optional<std::pair<int, int>> last_match_pos_;

      protected:
	std::shared_ptr<document> doc_;

	bool is_mouse_selecting_{false};
	int mouse_sel_start_char_{-1};
	int mouse_sel_start_line_{-1};
	int mouse_sel_end_char_{-1};
	int mouse_sel_end_line_{-1};

      private:
	event_queue window_queue_;
	std::vector<window *> linked_windows_;
};
