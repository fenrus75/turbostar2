#include "ui/components/ui_checkbox_group.h"
#include <algorithm>

ui_checkbox_group::ui_checkbox_group(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

ui_checkbox_group::ui_checkbox_group(std::string name) : ui_container(std::move(name), 0, 0, 0, 0), want_stretch_(true)
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
	for (const auto &child : children_) {
		child->flow();
	}

	int target_width = width() > 4 ? width() - 4 : 0;

	int running_y = 1;
	for (const auto &child : children_) {
		int target_x = 2;
		int target_y = running_y;
		if (child->x() != target_x || child->y() != target_y || (target_width > 0 && child->width() != target_width)) {
			if (target_width > 0) {
				child->set_width(target_width);
			}
			child->set_position(target_x, target_y);
		}
		running_y += child->height();
	}

	int target_height = running_y;
	bool dimensions_changed = (height() != target_height);
	if (dimensions_changed) {
		set_height(target_height);
	}

	return dimensions_changed;
}

int ui_checkbox_group::natural_width() const
{
	int max_child = 0;
	for (const auto &child : children_) {
		max_child = std::max(max_child, child->natural_width());
	}
	return max_child + 2; // account for x = 2 alignment
}
