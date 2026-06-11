#pragma once
#include <string>
#include "ui/ui_element.h"

class ui_text_label : public ui_element
{
      public:
	ui_text_label(int x, int y, const std::string &text);
	ui_text_label(const std::string &text, bool centered = false);
	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &, int, int) override
	{
		return false;
	}
	void set_text(const std::string &text)
	{
		text_ = text;
		set_width(text_.length());
	}

      private:
	std::string text_;
	bool centered_{false};
};
