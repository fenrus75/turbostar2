#pragma once

#include <memory>
#include <string>
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

	/**
	 * @brief Processes all pending events in the window's local queue.
	 * @return true if an event was processed that requires a re-render.
	 */
	bool process_events();
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

	mutable int top_line_{0};
	mutable int left_column_{0};

	std::shared_ptr<document> doc_;
	event_queue window_queue_;
};
