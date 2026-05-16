#pragma once
#include <string>

class status_bar
{
      public:
	status_bar() = default;
	~status_bar() = default;

	void draw(const std::string &mode_help = "", int cursor_x = -1,
		  int cursor_y = -1) const;
};
