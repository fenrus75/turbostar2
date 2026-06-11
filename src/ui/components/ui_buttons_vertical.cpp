#include "ui/components/ui_buttons_vertical.h"
#include <algorithm>

ui_buttons_vertical::ui_buttons_vertical(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

bool ui_buttons_vertical::flow()
{
	// Call flow on all children first, collect if any child's flow changed
	bool children_changed = false;
	for (const auto &child : children_) {
		if (child->flow()) {
			children_changed = true;
		}
	}

	// Find the maximum natural width of all children (buttons)
	int max_natural_width = 0;
	for (const auto &child : children_) {
		max_natural_width = std::max(max_natural_width, child->natural_width());
	}

	// Set width and position of all children in a loop
	bool layout_changed = false;
	int running_y = 0;
	for (const auto &child : children_) {
		int target_width = max_natural_width;
		int target_x = 0;
		int target_y = running_y;

		if (child->width() != target_width || child->x() != target_x || child->y() != target_y) {
			layout_changed = true;
			child->set_width(target_width);
			child->set_position(target_x, target_y);
		}

		running_y += 2;
	}

	int total_width = max_natural_width;
	int total_height = running_y;

	if (this->width() != total_width || this->height() != total_height) {
		layout_changed = true;
		this->set_width(total_width);
		this->set_height(total_height);
	}

	return children_changed || layout_changed;
}
