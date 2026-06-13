#pragma once
#include <string>
#include "ui/ui_element.h"

class ui_buttons_vertical : public ui_container
{
      public:
	ui_buttons_vertical(std::string name, int x, int y, int width, int height);

	bool flow() override;
	int natural_width() const override;
	int natural_height() const override;
};
