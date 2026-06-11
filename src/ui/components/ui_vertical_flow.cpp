#include "ui/components/ui_vertical_flow.h"
#include <algorithm>

ui_vertical_flow::ui_vertical_flow(std::string name, int x, int y, int x_offset, int y_offset, int spacer)
    : ui_container(std::move(name), x, y, 0, 0), x_offset_(x_offset), y_offset_(y_offset), spacer_(spacer)
{
}

ui_vertical_flow::ui_vertical_flow(std::string name, int x_offset, int y_offset, int spacer)
    : ui_container(std::move(name), 0, 0, 0, 0), x_offset_(x_offset), y_offset_(y_offset), spacer_(spacer)
{
}

bool ui_vertical_flow::flow()
{
	// Call flow on all children first, collect if any child's flow changed
	for (const auto &child : children_) {
		child->flow();
	}

	// Find the maximum natural width of all children
	int max_child_width = 0;
	for (const auto &child : children_) {
		max_child_width = std::max(max_child_width, child->natural_width());
	}

	int total_width = children_.empty() ? 0 : (2 * x_offset_ + max_child_width);

	// Set position and width of all children in a loop
	int running_y = y_offset_;
	for (const auto &child : children_) {
		int target_x = child->want_horizontal_stretch() ? 0 : x_offset_;
		int target_y = running_y;
		int target_width = child->want_horizontal_stretch() ? total_width : child->width();

		if (child->x() != target_x || child->y() != target_y || child->width() != target_width) {
			child->set_width(target_width);
			child->set_position(target_x, target_y);
		}

		running_y += child->height() + spacer_;
	}

	int total_height = children_.empty() ? 0 : (running_y - spacer_ + y_offset_);

	bool dimensions_changed = (this->width() != total_width || this->height() != total_height);
	if (dimensions_changed) {
		this->set_width(total_width);
		this->set_height(total_height);
	}

	return dimensions_changed;
}
