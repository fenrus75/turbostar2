#pragma once

#include "ui/ui_element.h"
#include <string>
#include <vector>

struct thumbnail_cell {
	int fg_r = 0;
	int fg_g = 0;
	int fg_b = 0;
	int bg_r = 0;
	int bg_g = 0;
	int bg_b = 0;
	std::string ch = " ";
};

class ui_thumbnail : public ui_element
{
      public:
	ui_thumbnail(std::string name, int x, int y, int width, int height);
	~ui_thumbnail() override = default;

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

	void set_image_path(const std::string &image_path);
	void clear_image();

      private:
	void fetch_thumbnail_data();

	std::string image_path_;
	int grid_width_ = 0;
	int grid_height_ = 0;
	std::vector<thumbnail_cell> cells_;
	bool has_data_ = false;
};
