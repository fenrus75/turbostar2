#pragma once

#include <vector>
#include <string>
#include "event_queue.h"

struct menu_item {
	std::string name;
	event_type action;
	char hotkey{0};
	std::string shortcut;
	menu_item(std::string n, event_type a, char h, std::string s, bool sep) : name(n), action(a), hotkey(h), shortcut(s), is_separator(sep) {}
	bool is_separator{false};
};

struct menu_category {
	std::string name;
	char hotkey;
	std::vector<menu_item> items;
};

class menu_bar {
public:
	menu_bar();
	~menu_bar() = default;

	void draw() const;
	bool handle_alt_key(char c, event_queue& queue);
	bool handle_key(int key, event_queue& queue);
	bool is_open() const;
	void close_menu();

private:
	std::vector<menu_category> categories_;
	int active_category_{-1};
	int selected_item_{0};
};
