#pragma once
#include "ui/ui_element.h"
#include <string>

class ui_text_label : public ui_element {
public:
	ui_text_label(int x, int y, const std::string& text);
	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event&, int, int) override { return false; }
private:
	std::string text_;
};
