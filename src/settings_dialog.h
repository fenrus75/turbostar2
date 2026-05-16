#pragma once

#include <string>
#include <vector>
#include "dialog.h"

/**
 * @brief Preferences dialog inspired by Borland Turbo C++.
 */
class settings_dialog : public dialog
{
      public:
	settings_dialog();
	~settings_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	
	std::string get_selected_style() const;

      private:
	void draw_group_box(int gy, int gx, int gw, int gh, const std::string &title) const;
	void draw_radio_button(int gy, int gx, const std::string &label, bool selected, char hotkey) const;

	std::vector<std::string> styles_;
	int selected_style_idx_{0};
	int focus_idx_{0}; // 0 = Styles group, 1 = OK, 2 = Cancel, 3 = Help
};
