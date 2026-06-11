#include "ui/components/ui_checkbox_group.h"
#include <algorithm>

ui_checkbox_group::ui_checkbox_group(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

/*
 * Computes the vertical layout flow for child checkboxes within the group.
 * Positions checkboxes consecutively starting from x = 2 and y = 1
 * (avoiding group box borders) with no spacer between them.
 * Dynamically adjusts the group container's height to enclose all child checkboxes.
 */
bool ui_checkbox_group::flow()
{
	bool children_changed = false;
	for (const auto &child : children_) {
		if (child->flow()) {
			children_changed = true;
		}
	}

	bool layout_changed = false;
	int running_y = 1;
	for (const auto &child : children_) {
		int target_x = 2;
		int target_y = running_y;
		if (child->x() != target_x || child->y() != target_y) {
			layout_changed = true;
			child->set_position(target_x, target_y);
		}
		running_y += child->height();
	}

	int target_height = running_y;
	if (height() != target_height) {
		layout_changed = true;
		set_height(target_height);
	}

	return children_changed || layout_changed;
}
