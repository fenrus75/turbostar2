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
	std::string get_build_system() const;
	std::string get_build_directory() const;

      private:
	void draw_group_box(int gy, int gx, int gw, int gh, const std::string &title) const;
	void draw_radio_button(int gy, int gx, const std::string &label, bool selected, char hotkey) const;
	void draw_text_input(int gy, int gx, int gw, const std::string &label, const std::string &value, bool focused) const;

	std::vector<std::string> styles_;
	int selected_style_idx_{0};
	
	std::vector<std::string> build_systems_;
	int selected_build_system_idx_{0};
	
	std::string build_directory_buffer_;
	int focus_idx_{0}; // 0 = Styles, 1 = Build System, 2 = Build Dir, 3 = OK, 4 = Cancel, 5 = Help
};
