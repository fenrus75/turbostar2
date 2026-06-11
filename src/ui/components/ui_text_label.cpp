#include "ui/components/ui_text_label.h"
#include <ncurses.h>

ui_text_label::ui_text_label(int x, int y, const std::string &text)
    : ui_element("text", x, y, text.length(), 1), text_(text), centered_(false)
{
}

ui_text_label::ui_text_label(const std::string &text, bool centered)
    : ui_element("text", 0, 0, text.length(), 1), text_(text), centered_(centered)
{
}

void ui_text_label::draw(int abs_x, int abs_y) const
{
	attron(COLOR_PAIR(1));
	int draw_x = abs_x;
	if (centered_) {
		draw_x += (width_ - static_cast<int>(text_.length())) / 2;
	}
	mvaddstr(abs_y, draw_x, text_.c_str());
	attroff(COLOR_PAIR(1));
}

int ui_text_label::natural_width() const
{
	return static_cast<int>(text_.length());
}
