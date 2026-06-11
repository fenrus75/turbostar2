#pragma once
#include "ui/ui_element.h"
#include <functional>
#include <string>
#include <optional>
#include <vector>
#include <filesystem>
#include <chrono>

class ui_group_box : public ui_container
{
      public:
	ui_group_box(std::string name, int x, int y, int width, int height, const std::string &title);

	void draw(int abs_x, int abs_y) const override;
	bool flow() override;
	// handle_event is inherited from ui_container, so it just dispatches to children

      private:
	std::string title_;
};
