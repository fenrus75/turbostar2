#pragma once
#include "ui/ui_element.h"
#include <string>

class ui_vertical_flow : public ui_container
{
      public:
	ui_vertical_flow(std::string name, int x, int y, int x_offset, int y_offset);
	ui_vertical_flow(std::string name, int x_offset = 0, int y_offset = 0);

	bool flow() override;

	int x_offset() const { return x_offset_; }
	void set_x_offset(int x_offset) { x_offset_ = x_offset; }

	int y_offset() const { return y_offset_; }
	void set_y_offset(int y_offset) { y_offset_ = y_offset; }

      private:
	int x_offset_;
	int y_offset_;
};
