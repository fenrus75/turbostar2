#pragma once
#include <string>
#include "ui/ui_element.h"

class ui_grid_flow : public ui_container
{
      public:
	ui_grid_flow(std::string name, int x, int y, int x_offset, int y_offset, int h_spacer, int v_spacer, int columns);
	ui_grid_flow(std::string name, int x_offset = 0, int y_offset = 0, int h_spacer = 2, int v_spacer = 1, int columns = 0);

	bool flow() override;
	int natural_width() const override;
	int natural_height() const override;

	int x_offset() const { return x_offset_; }
	void set_x_offset(int x_offset) { x_offset_ = x_offset; }

	int y_offset() const { return y_offset_; }
	void set_y_offset(int y_offset) { y_offset_ = y_offset; }

	int h_spacer() const { return h_spacer_; }
	void set_h_spacer(int h_spacer) { h_spacer_ = h_spacer; }

	int v_spacer() const { return v_spacer_; }
	void set_v_spacer(int v_spacer) { v_spacer_ = v_spacer; }

	int columns() const { return columns_; }
	void set_columns(int columns) { columns_ = columns; }

      private:
	int x_offset_;
	int y_offset_;
	int h_spacer_;
	int v_spacer_;
	int columns_;
};
