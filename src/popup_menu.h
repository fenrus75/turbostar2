#pragma once

#include <string>
#include <vector>
#include <optional>

struct popup_menu_item {
	int id;
	std::string name;
	char hotkey;
	bool is_separator{false};
};

class popup_menu {
      public:
	popup_menu(int x, int y, const std::vector<popup_menu_item>& items);
	~popup_menu() = default;

	void draw() const;
	
	// Returns the ID of the selected item, a special cancel ID (-1), or nullopt if event unhandled
	std::optional<int> handle_key(int key);
	
	// Returns the ID of the clicked item, a special cancel ID (-1) if clicked outside, or nullopt if just hovering
	std::optional<int> handle_mouse(int mouse_x, int mouse_y);

	static constexpr int cancel_id = -1;

      private:
	int x_, y_;
	int width_, height_;
	std::vector<popup_menu_item> items_;
	int selected_idx_{0};
};
