#pragma once
#include "ui/ui_element.h"
#include <functional>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <chrono>

class ui_checkbox : public ui_element
{
      public:
	ui_checkbox(std::string name, const std::string &text, char hotkey, bool initial_state = false);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;

      private:
	std::string text_;
	char hotkey_;
	bool checked_;
};
