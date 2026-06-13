#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include "ui/ui_element.h"

class ui_group_box : public ui_container
{
      public:
	ui_group_box(std::string name, int x, int y, int width, int height, const std::string &title);
	ui_group_box(std::string name, int width, const std::string &title);

	void draw(int abs_x, int abs_y) const override;
	bool flow() override;
	int natural_width() const override;
	int natural_height() const override;
	// handle_event is inherited from ui_container, so it just dispatches to children

      private:
	std::string title_;
	bool auto_height_{false};
};
