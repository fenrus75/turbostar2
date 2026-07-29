#pragma once
#include <optional>
#include <string>
#include "ui/ui_element.h"

/*
 * ui_checkbox_group is a layout container for multiple independent checkboxes.
 * It positions its children vertically consecutively with zero spacer.
 */
class ui_checkbox_group : public ui_container
{
      public:
	ui_checkbox_group(std::string name, int x, int y, int width, int height);
	ui_checkbox_group(std::string name);

	bool flow() override;
	bool want_horizontal_stretch() const override
	{
		return false;
	}
	int natural_width() const override;
	int natural_height() const override;

	bool focus_first() override;
	bool focus_last() override;
	bool focus_next() override;
	bool focus_previous() override;
	std::vector<ui_element *> get_focusable_elements() override;

      private:
	bool want_stretch_{false};
};
