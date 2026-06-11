#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "ui/ui_element.h"

class ui_radio_choice : public ui_element
{
      public:
	ui_radio_choice(std::string name, const std::string &text, char hotkey, bool initial_state = false);

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;
	int natural_width() const override;
	bool is_focusable() const override { return true; }

	void set_selected(bool s)
	{
		selected_ = s;
	}
	bool is_selected() const
	{
		return selected_;
	}

      private:
	std::string text_;
	char hotkey_;
	bool selected_;
};

class ui_radiobutton_group : public ui_container
{
      public:
	ui_radiobutton_group(std::string name, int x, int y, int width, int height, bool horizontal = false);
	ui_radiobutton_group(std::string name, bool horizontal = false);

	bool flow() override;
	bool want_horizontal_stretch() const override
	{
		return false;
	}
	int natural_width() const override;

	void child_got_selected(ui_element *child) override;
	std::optional<std::string> get_value(const std::string &target_name) const override;

      private:
	bool horizontal_;
	bool want_stretch_{false};
};
