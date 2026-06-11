#include "ui/components/ui_buttons_horizontal.h"
#include <algorithm>

ui_buttons_horizontal::ui_buttons_horizontal(std::string name, int x, int y, int width, int height)
    : ui_container(std::move(name), x, y, width, height)
{
}

bool ui_buttons_horizontal::flow()
{
	// Call flow on all children first, collect if any child's flow changed
	for (const auto &child : children_) {
		child->flow();
	}

	// Find the maximum natural width of all children (buttons)
	int max_natural_width = 0;
	for (const auto &child : children_) {
		max_natural_width = std::max(max_natural_width, child->natural_width());
	}

	int total_buttons_width =
	    children_.empty() ? 0 : static_cast<int>(children_.size()) * max_natural_width + (static_cast<int>(children_.size()) - 1) * 2;
	int start_x = 0;
	if (centered_ && this->width() > 0) {
		start_x = (this->width() - total_buttons_width) / 2;
	}

	// Set width and position of all children in a loop
	int running_x = start_x;
	for (const auto &child : children_) {
		int target_width = max_natural_width;
		int target_x = running_x;
		int target_y = 0;

		if (child->width() != target_width || child->x() != target_x || child->y() != target_y) {
			child->set_width(target_width);
			child->set_position(target_x, target_y);
		}

		running_x += target_width + 2;
	}

	int target_width = (centered_ && this->width() > 0) ? this->width() : total_buttons_width;
	int target_height = 2;

	bool dimensions_changed = (this->width() != target_width || this->height() != target_height);
	if (dimensions_changed) {
		this->set_width(target_width);
		this->set_height(target_height);
	}

	return dimensions_changed;
}

bool ui_buttons_horizontal::want_horizontal_stretch() const
{
	return centered_;
}

int ui_buttons_horizontal::natural_width() const
{
	int max_natural = 0;
	for (const auto &child : children_) {
		max_natural = std::max(max_natural, child->natural_width());
	}
	return children_.empty() ? 0 : static_cast<int>(children_.size()) * max_natural + (static_cast<int>(children_.size()) - 1) * 2;
}
