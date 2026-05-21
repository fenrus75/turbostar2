#pragma once

#include <string>
#include <vector>
#include "ui/dialog.h"
#include "document.h" // For search_params

/**
 * @brief Advanced search dialog inspired by Turbo Pascal 7.
 *
 * Features multiple sections: Options, Direction, Scope, and Origin.
 */
class find_dialog_legacy : public dialog
{
      public:
	find_dialog_legacy(const std::string &title, const search_params &initial_params, bool is_replace = false);
	~find_dialog_legacy() override = default;

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
	enum class focus_item {
		query,
		replacement,
		case_sensitive,
		whole_words,
		regex,
		prompt_replace,
		dir_forward,
		dir_backward,
		scope_global,
		scope_selected,
		origin_cursor,
		origin_entire,
		btn_ok,
		btn_change_all,
		btn_cancel
	};
	focus_item focus_idx_{focus_item::query};
};
