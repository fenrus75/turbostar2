#include "ui/components/ui_group_box.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_group_box ---

ui_group_box::ui_group_box(std::string name, int x, int y, int width, int height, const std::string &title)
    : ui_container(std::move(name), x, y, width, height), title_(title)
{
}

void ui_group_box::draw(int abs_x, int abs_y) const
{
	attrset(COLOR_PAIR(17));
	for (int i = 1; i < height_; ++i) {
		move(abs_y + i, abs_x);
		for (int j = 0; j < width_; ++j)
			addch(' ');
	}

	if (!title_.empty()) {
		attrset(COLOR_PAIR(1));
		mvaddstr(abs_y, abs_x, title_.c_str());
	}
	attrset(0);

	// Draw children
	ui_container::draw(abs_x, abs_y);
}

#include <algorithm>
#include <sys/stat.h>
#include "fs_utils.h"
