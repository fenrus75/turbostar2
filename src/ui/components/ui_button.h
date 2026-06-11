#pragma once
#include "ui/ui_element.h"
#include <functional>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <chrono>

class ui_button : public ui_element
{
      public:
	ui_button(std::string name, int x, int y, const std::string &text, char hotkey, std::function<void()> on_click, bool press_on_esc = false);
	ui_button(std::string name, const std::string &text, char hotkey, std::function<void()> on_click, bool press_on_esc = false);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	int natural_width() const override;

      private:
	std::string text_;
	char hotkey_;
	std::function<void()> on_click_;
};
