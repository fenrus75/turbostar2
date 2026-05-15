#pragma once

#include <string>
#include "event_queue.h"

class window {
public:
	window(int id, int x, int y, int width, int height, const std::string& title);
	~window() = default;

	void draw() const;
	void set_active(bool active);
	bool is_active() const;

	event_queue& get_queue();

private:
	void draw_border() const;
	void draw_content() const;

	int id_;
	int x_, y_, width_, height_;
	std::string title_;
	bool is_active_{false};

	int top_line_{0};
	int left_column_{0};

	event_queue window_queue_;
};
