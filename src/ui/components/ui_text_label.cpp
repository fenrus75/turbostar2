#include "ui/components/ui_text_label.h"
#include <ncurses.h>

ui_text_label::ui_text_label(int x, int y, const std::string& text)
	: ui_element("text", x, y, text.length(), 1), text_(text) {}

void ui_text_label::draw(int abs_x, int abs_y) const {
	attron(COLOR_PAIR(1));
	mvaddstr(abs_y, abs_x, text_.c_str());
	attroff(COLOR_PAIR(1));
}
