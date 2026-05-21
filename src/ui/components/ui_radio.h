#pragma once
#include "ui/ui_element.h"
#include <functional>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <chrono>

class ui_radio_choice : public ui_element
{
      public:
	ui_radio_choice(std::string name, int x, int y, const std::string &text, char hotkey, bool initial_state = false);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;

	void set_selected(bool s) { selected_ = s; }
	bool is_selected() const { return selected_; }

      private:
	std::string text_;
	char hotkey_;
	bool selected_;
};

class ui_radiobutton_group : public ui_container
{
      public:
	ui_radiobutton_group(std::string name, int x, int y, int width, int height);

	void child_got_selected(ui_element *child) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
};
