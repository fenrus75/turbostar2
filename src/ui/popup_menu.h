#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

struct popup_menu_item {
	int id{0};
	std::string name;
	char hotkey{0};
	bool is_separator{false};
	std::function<void()> on_select{};
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

	std::function<void()> get_selected_callback() const
	{
		if (selected_idx_ >= 0 && selected_idx_ < static_cast<int>(items_.size())) {
			return items_[selected_idx_].on_select;
		}
		return {};
	}

	void update_item(size_t index, const std::string &new_name, char new_hotkey, std::function<void()> on_select = {})
	{
		if (index < items_.size()) {
			items_[index].name = new_name;
			items_[index].hotkey = new_hotkey;
			if (on_select) {
				items_[index].on_select = std::move(on_select);
			}
		}
	}

	static constexpr int cancel_id = -1;

      private:
	int x_, y_;
	int width_, height_;
	std::vector<popup_menu_item> items_;
	int selected_idx_{0};
};
