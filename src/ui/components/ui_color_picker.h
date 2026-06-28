#pragma once
#include <functional>
#include <optional>
#include <string>
#include "ui/ui_element.h"

class ui_color_picker : public ui_element
{
      public:
	ui_color_picker(std::string name, int x, int y, uint8_t initial_fg = 15, uint8_t initial_bg = 1, uint8_t dialog_bg = 7);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	int natural_width() const override { return 50; }
	int natural_height() const override { return 6; }
	bool is_focusable() const override { return true; }

	uint8_t selected_fg() const { return selected_fg_; }
	uint8_t selected_bg() const { return selected_bg_; }

      private:
	uint8_t selected_fg_;
	uint8_t selected_bg_;
	uint8_t dialog_bg_;
	mutable bool focus_bg_row_{false};
};
