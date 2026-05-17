#pragma once

#include <memory>
#include <string>
#include <chrono>
#include "document.h"
#include "event_queue.h"

class window
{
      public:
	window(int id, int x, int y, int width, int height, const std::string &title);
	~window() = default;

	void draw() const;
	void set_active(bool active);
	bool is_active() const;

	void attach_document(std::shared_ptr<document> doc);
	/**
	 * @brief Returns the window's local event queue.
	 */
	event_queue &get_queue();

	uint64_t get_last_active_timestamp() const { return last_active_timestamp_; }
	void update_last_active_timestamp();

	int get_display_priority() const { return display_priority_; }
	void set_display_priority(int priority) { display_priority_ = priority; }

	bool is_visible() const { return is_visible_; }
	void set_visible(bool visible) { is_visible_ = visible; }

	int get_x() const { return x_; }
	int get_y() const { return y_; }
	int get_width() const { return width_; }
	int get_height() const { return height_; }

	int get_background_color_pair() const { return background_color_pair_; }
	void set_background_color_pair(int pair) { background_color_pair_ = pair; }

	/**
	 * @brief Processes all pending events in the window's local queue.
	 * @return true if an event was processed that requires a re-render.
	 */
	bool process_events();
	void invalidate();
	void set_cursor_position() const;
	int get_cursor_x() const;
	int get_cursor_y() const;

	std::string get_title() const
	{
		return title_;
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

      private:
	void update_viewport() const;
	void draw_border() const;
	void draw_content() const;

	int id_;
	int x_, y_, width_, height_;
	std::string title_;
	bool is_active_{false};

	uint64_t last_active_timestamp_{0};
	int display_priority_{0};
	bool is_visible_{true};

	int background_color_pair_{3};

	mutable int top_line_{0};
	mutable int left_column_{0};

	bool needs_render_{false};
	std::shared_ptr<document> doc_;
	event_queue window_queue_;
};
