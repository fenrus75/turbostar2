#pragma once
#include "ui/ui_element.h"
#include <string>
#include <optional>

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
	bool want_horizontal_stretch() const override { return want_stretch_; }

      private:
	bool want_stretch_{false};
};
