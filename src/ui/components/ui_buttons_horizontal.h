#pragma once
#include <string>
#include "ui/ui_element.h"

class ui_buttons_horizontal : public ui_container
{
      public:
	ui_buttons_horizontal(std::string name, int x, int y, int width, int height);

	bool flow() override;
	bool want_horizontal_stretch() const override;
	int natural_width() const override;

	bool centered() const
	{
		return centered_;
	}
	void set_centered(bool centered)
	{
		centered_ = centered;
	}

      private:
	bool centered_{false};
};
