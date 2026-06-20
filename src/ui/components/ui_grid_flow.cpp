#include "ui/components/ui_grid_flow.h"
#include <algorithm>
#include <cmath>

ui_grid_flow::ui_grid_flow(std::string name, int x, int y, int x_offset, int y_offset, int h_spacer, int v_spacer, int columns)
    : ui_container(std::move(name), x, y, 0, 0), x_offset_(x_offset), y_offset_(y_offset), h_spacer_(h_spacer), v_spacer_(v_spacer), columns_(columns)
{
}

ui_grid_flow::ui_grid_flow(std::string name, int x_offset, int y_offset, int h_spacer, int v_spacer, int columns)
    : ui_container(std::move(name), 0, 0, 0, 0), x_offset_(x_offset), y_offset_(y_offset), h_spacer_(h_spacer), v_spacer_(v_spacer), columns_(columns)
{
}

bool ui_grid_flow::flow()
{
	// Propagate layout updates to children first so their own dynamic natural dimensions are accurate.
	for (const auto &child : children_) {
		child->flow();
	}

	if (children_.empty()) {
		bool dimensions_changed = (this->width() != 0 || this->height() != 0);
		if (dimensions_changed) {
			this->set_width(0);
			this->set_height(0);
		}
		return dimensions_changed;
	}

	// Calculate uniform cell sizing: every cell takes the maximum natural size required by any child element.
	int cell_width = 0;
	int cell_height = 0;
	for (const auto &child : children_) {
		cell_width = std::max(cell_width, child->natural_width());
		cell_height = std::max(cell_height, child->natural_height());
	}

	// Determine the number of columns to use for this layout pass.
	int cols = 1;
	if (columns_ > 0) {
		cols = columns_;
	} else {
		// Auto-wrap mode: fit as many columns as possible within the allocated width.
		int avail_w = this->width() - 2 * x_offset_;
		if (avail_w > cell_width) {
			cols = (avail_w + h_spacer_) / (cell_width + h_spacer_);
		}
		if (cols < 1) {
			cols = 1;
		}
	}

	// Set uniform size and position for each child sequentially.
	for (size_t i = 0; i < children_.size(); ++i) {
		int row = static_cast<int>(i / cols);
		int col = static_cast<int>(i % cols);

		int target_x = x_offset_ + col * (cell_width + h_spacer_);
		int target_y = y_offset_ + row * (cell_height + v_spacer_);

		auto &child = children_[i];
		if (child->x() != target_x || child->y() != target_y || child->width() != cell_width || child->height() != cell_height) {
			child->set_width(cell_width);
			child->set_height(cell_height);
			child->set_position(target_x, target_y);
		}
	}

	// Compute overall grid dimensions.
	int rows = static_cast<int>((children_.size() + cols - 1) / cols);
	int total_width = 2 * x_offset_ + cols * cell_width + (cols - 1) * h_spacer_;
	int total_height = 2 * y_offset_ + rows * cell_height + (rows - 1) * v_spacer_;

	// Notify layout parent if our own container boundaries shifted.
	bool dimensions_changed = (this->width() != total_width || this->height() != total_height);
	if (dimensions_changed) {
		this->set_width(total_width);
		this->set_height(total_height);
	}

	return dimensions_changed;
}

int ui_grid_flow::natural_width() const
{
	if (children_.empty()) {
		return 0;
	}

	int cell_width = 0;
	for (const auto &child : children_) {
		cell_width = std::max(cell_width, child->natural_width());
	}

	int cols = 1;
	if (columns_ > 0) {
		cols = columns_;
	} else {
		// When unconstrained by allocated bounds, default to a balanced square/balanced grid.
		cols = static_cast<int>(std::ceil(std::sqrt(children_.size())));
		if (cols < 1) {
			cols = 1;
		}
	}

	return 2 * x_offset_ + cols * cell_width + (cols - 1) * h_spacer_;
}

int ui_grid_flow::natural_height() const
{
	if (children_.empty()) {
		return 0;
	}

	int cell_width = 0;
	int cell_height = 0;
	for (const auto &child : children_) {
		cell_width = std::max(cell_width, child->natural_width());
		cell_height = std::max(cell_height, child->natural_height());
	}

	int cols = 1;
	if (columns_ > 0) {
		cols = columns_;
	} else if (this->width() > 0) {
		// If allocated container width is known, wrap cells according to the available layout width.
		int avail_w = this->width() - 2 * x_offset_;
		if (avail_w > cell_width) {
			cols = (avail_w + h_spacer_) / (cell_width + h_spacer_);
		}
		if (cols < 1) {
			cols = 1;
		}
	} else {
		// Default to a balanced square grid if neither column count nor width is constrained.
		cols = static_cast<int>(std::ceil(std::sqrt(children_.size())));
		if (cols < 1) {
			cols = 1;
		}
	}

	int rows = static_cast<int>((children_.size() + cols - 1) / cols);
	return 2 * y_offset_ + rows * cell_height + (rows - 1) * v_spacer_;
}
