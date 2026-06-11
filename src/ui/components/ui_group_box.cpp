#include "ui/components/ui_group_box.h"
#include <algorithm>
#include <ctype.h>
#include <ncurses.h>
#include <sys/stat.h>
#include "fs_utils.h"

// --- ui_group_box ---

ui_group_box::ui_group_box(std::string name, int x, int y, int width, int height, const std::string &title)
    : ui_container(std::move(name), x, y, width, height), title_(title), auto_height_(false)
{
}

ui_group_box::ui_group_box(std::string name, int width, const std::string &title)
    : ui_container(std::move(name), 0, 0, width, 0), title_(title), auto_height_(true)
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

/*
 * Computes the layout flow for children within the group box container.
 * Finds the maximum bottom coordinate (y + height) across all child elements,
 * and dynamically resizes the group box's height to encapsulate them fully.
 */
bool ui_group_box::flow()
{
	// First, let any children that need to stretch get their widths updated based on our width
	for (const auto &child : children_) {
		if (child->want_horizontal_stretch()) {
			int target_width = width_ - 2;
			if (child->width() != target_width || child->x() != 2) {
				child->set_width(target_width);
				child->set_position(2, child->y());
			}
		}
	}

	ui_container::flow();

	bool dimensions_changed = false;
	if (auto_height_) {
		int max_height = 0;
		for (const auto &child : children_) {
			max_height = std::max(max_height, child->y() + child->height());
		}

		if (height_ != max_height && max_height > 0) {
			height_ = max_height;
			dimensions_changed = true;
		}
	}

	return dimensions_changed;
}
