#pragma once
#include "ui/ui_element.h"
#include <string>

class ui_buttons_horizontal : public ui_container
{
      public:
	ui_buttons_horizontal(std::string name, int x, int y, int width, int height);

	bool flow() override;
};
