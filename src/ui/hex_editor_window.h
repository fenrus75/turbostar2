#pragma once

#include <memory>
#include "../binary_document.h"
#include "window.h"

class hex_editor_window : public window
{
      public:
	hex_editor_window(int id, int x, int y, int width, int height, const std::string &title, std::shared_ptr<binary_document> doc);
	~hex_editor_window() override = default;

	void draw_content(bool cursor_only = false) const override;
	bool process_events() override;
	void set_cursor_position() const override;

	std::string get_status_help() const override;

      private:
	size_t get_bytes_per_line() const;

	bool hex_focus_{true};
	size_t cursor_offset_{0};
	int nibble_focus_{0}; // 0 = high, 1 = low
	mutable size_t scroll_line_{0};
};
