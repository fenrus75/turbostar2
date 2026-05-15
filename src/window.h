#pragma once

#include <string>
#include <memory>
#include "event_queue.h"
#include "document.h"

class window {
public:
	window(int id, int x, int y, int width, int height, const std::string& title);
	~window() = default;

	void draw() const;
	void set_active(bool active);
	bool is_active() const;

	void attach_document(std::shared_ptr<document> doc);
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

	std::shared_ptr<document> doc_;
	event_queue window_queue_;
};
