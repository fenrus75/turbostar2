#include "ui/components/ui_horizontal_flow.h"
#include <algorithm>

ui_horizontal_flow::ui_horizontal_flow(std::string name, int x, int y, int x_offset, int y_offset)
    : ui_container(std::move(name), x, y, 0, 0), x_offset_(x_offset), y_offset_(y_offset)
{
}

ui_horizontal_flow::ui_horizontal_flow(std::string name, int x_offset, int y_offset)
    : ui_container(std::move(name), 0, 0, 0, 0), x_offset_(x_offset), y_offset_(y_offset)
{
}

bool ui_horizontal_flow::flow()
{
	// Call flow on all children first, collect if any child's flow changed
	for (const auto &child : children_) {
		child->flow();
	}

	// Find the maximum height of all children
	int max_child_height = 0;
	for (const auto &child : children_) {
		max_child_height = std::max(max_child_height, child->height());
	}

	// Set position of all children in a loop
	int running_x = x_offset_;
	for (const auto &child : children_) {
		int target_width = child->natural_width();
		int target_x = running_x;
		int target_y = y_offset_;

		if (child->width() != target_width || child->x() != target_x || child->y() != target_y) {
			child->set_width(target_width);
			child->set_position(target_x, target_y);
		}

		running_x += target_width + 2;
	}

	int total_width = children_.empty() ? 0 : (running_x - 2 + x_offset_);
	int total_height = children_.empty() ? 0 : (2 * y_offset_ + max_child_height);

	int effective_width = std::max(this->width(), total_width);
	int effective_height = std::max(this->height(), total_height);
	bool dimensions_changed = (this->width() != effective_width || this->height() != effective_height);
	if (dimensions_changed) {
		this->set_width(effective_width);
		this->set_height(effective_height);
	}

	return dimensions_changed;
}

int ui_horizontal_flow::natural_width() const
{
	if (children_.empty()) {
		return 0;
	}
	int running_x = x_offset_;
	for (const auto &child : children_) {
		running_x += child->natural_width() + 2;
	}
	return running_x - 2 + x_offset_;
}

int ui_horizontal_flow::natural_height() const
{
	if (children_.empty()) {
		return 0;
	}
	int max_child_height = 0;
	for (const auto &child : children_) {
		max_child_height = std::max(max_child_height, child->natural_height());
	}
	return 2 * y_offset_ + max_child_height;
}
