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

	bool flow() override;
};
