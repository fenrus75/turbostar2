#pragma once

#include "ui/ui_element.h"
#include <string>
#include <vector>

struct thumbnail_pixel {
	int r = 0;
	int g = 0;
	int b = 0;
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
	std::vector<thumbnail_pixel> pixels_;
	bool has_data_ = false;
};
