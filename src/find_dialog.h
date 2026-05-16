#pragma once

#include <string>
#include <vector>
#include "dialog.h"
#include "document.h" // For search_params

/**
 * @brief Advanced search dialog inspired by Turbo Pascal 7.
 *
 * Features multiple sections: Options, Direction, Scope, and Origin.
 */
class find_dialog : public dialog
{
      public:
	find_dialog(const std::string &title, const search_params &initial_params, bool is_replace = false);
	~find_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	search_params get_search_params() const;

      private:
	void draw_group_box(int y, int x, int w, int h, const std::string &title) const;
	void draw_labeled_text(int y, int x, const std::string &text, char hotkey) const;
	void draw_group_labeled_text(int ly, int lx, const std::string &text, char hotkey) const;
	int get_height(bool is_replace);

	search_params params_;
	bool is_replace_{false};
	int focus_idx_{0};
};
