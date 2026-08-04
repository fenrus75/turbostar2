#pragma once

#include <string>
#include <vector>
#include "event_queue.h"

struct menu_item {
	std::string name;
	event_type action{event_type::key_press};
	int action_key_code{0};
	char hotkey{0};
	std::string shortcut;
	std::string payload;
	bool is_separator{false};
	bool is_disabled{false};

	// Submenu support
	std::vector<menu_item> submenu_items;

	menu_item(std::string n, event_type a, char h, std::string s, bool sep)
	    : name(n), action(a), hotkey(h), shortcut(s), is_separator(sep)
	{
	}
	menu_item(std::string n, event_type a, int ak, char h, std::string s, bool sep)
	    : name(n), action(a), action_key_code(ak), hotkey(h), shortcut(s), is_separator(sep)
	{
	}
	menu_item(std::string n, event_type a, std::string p, char h, std::string s, bool sep)
	    : name(n), action(a), hotkey(h), shortcut(s), payload(p), is_separator(sep)
	{
	}
	menu_item(std::string n, std::vector<menu_item> sub, char h)
	    : name(n), hotkey(h), submenu_items(std::move(sub))
	{
	}

	bool has_submenu() const { return !submenu_items.empty(); }
};

struct menu_category {
	std::string name;
	char hotkey;
	std::vector<menu_item> items;
};

class menu_bar
{
      public:
	menu_bar();
	~menu_bar() = default;

	void draw() const;
	bool handle_alt_key(char c, event_queue &queue);
	bool handle_key(int key, event_queue &queue);
	bool handle_mouse(int x, int y, event_queue &queue);
	bool is_open() const;
	void close_menu();

	void set_category_items(const std::string &name, const std::vector<menu_item> &items);
	void set_item_disabled(event_type action, bool disabled);

	// Query submenu state for unit testing
	bool is_submenu_open() const { return submenu_open_; }
	int get_selected_submenu_item() const { return selected_submenu_item_; }

      private:
	void select_category(int index);
	void find_next_item();
	void find_prev_item();
	void find_next_submenu_item();
	void find_prev_submenu_item();

	std::vector<menu_category> categories_;
	int active_category_{-1};
	int selected_item_{0};

	// Submenu navigation state
	mutable bool submenu_open_{false};
	mutable int selected_submenu_item_{0};
};
