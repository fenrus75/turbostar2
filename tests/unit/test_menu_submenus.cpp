#include "test_watchdog.h"
#include "history_manager.h"
#include "ui/menu_bar.h"
#include "event_queue.h"
#include <ncurses.h>
#include <cassert>
#include <iostream>

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing menu_bar submenus and history_manager Open Recent integration...\n";

	// 1. Test menu_item constructors and submenu methods
	menu_item child1("Child 1", event_type::open_file, "/path/file1.cpp", '1', "", false);
	menu_item child2("Child 2", event_type::open_file, "/path/file2.cpp", '2', "", false);
	std::vector<menu_item> sub_children = {child1, child2};

	menu_item parent_item("Parent", sub_children, 'p');
	assert(parent_item.has_submenu());
	assert(parent_item.submenu_items.size() == 2);
	assert(parent_item.submenu_items[0].payload == "/path/file1.cpp");
	assert(parent_item.submenu_items[1].payload == "/path/file2.cpp");

	// 2. Test menu_bar navigation with submenus
	menu_bar menu;
	event_queue queue;

	std::vector<menu_item> test_file_items = {
		{"New Project...", event_type::new_project, 'p', "", false},
		{"New File", event_type::new_doc, 'n', "^KN", false},
		{"Open...", event_type::load, 'o', "^KE", false},
		menu_item("Open Recent...", sub_children, 'r'),
		{"Exit", event_type::quit, 'x', "^KQ", false}
	};
	menu.set_category_items("File", test_file_items);

	// Open File menu (category index 0)
	menu.handle_alt_key('f', queue);
	assert(menu.is_open());
	assert(!menu.is_submenu_open());

	// Navigate down to "Open Recent..." (index 3: New Project [0], New File [1], Open [2], Open Recent [3])
	menu.handle_key(KEY_DOWN, queue); // [1] New File
	menu.handle_key(KEY_DOWN, queue); // [2] Open...
	menu.handle_key(KEY_DOWN, queue); // [3] Open Recent...
	assert(!menu.is_submenu_open());

	// Press KEY_RIGHT to open submenu
	menu.handle_key(KEY_RIGHT, queue);
	assert(menu.is_submenu_open());
	assert(menu.get_selected_submenu_item() == 0);

	// Press ESC to close submenu but keep parent menu open
	menu.handle_key(27, queue);
	assert(menu.is_open());
	assert(!menu.is_submenu_open());

	// Press KEY_RIGHT to open submenu again
	menu.handle_key(KEY_RIGHT, queue);
	assert(menu.is_submenu_open());

	// Press KEY_LEFT to return focus to parent dropdown
	menu.handle_key(KEY_LEFT, queue);
	assert(menu.is_open());
	assert(!menu.is_submenu_open());

	// 3. Test history_manager integration with dynamic menu items
	history_manager::get_instance().add_file("/tmp/test_recent_alpha.cpp");
	history_manager::get_instance().add_file("/tmp/test_recent_beta.cpp");

	const auto &recent_files = history_manager::get_instance().get_files();
	assert(!recent_files.empty());
	assert(recent_files.front() == "/tmp/test_recent_beta.cpp");

	// Populate dynamic submenu items
	std::vector<menu_item> dynamic_sub_items;
	size_t count = 0;
	for (const auto &fp : recent_files) {
		if (count >= 5) break;
		menu_item item(fp, event_type::open_file, fp, static_cast<char>('1' + count), "", false);
		dynamic_sub_items.push_back(item);
		count++;
	}

	std::vector<menu_item> file_menu_items = {
		{"New Project...", event_type::new_project, 'p', "", false},
		{"New File", event_type::new_doc, 'n', "^KN", false},
		{"Open...", event_type::load, 'o', "^KE", false},
		menu_item("Open Recent...", dynamic_sub_items, 'r'),
		{"Exit", event_type::quit, 'x', "^KQ", false}
	};
	menu.set_category_items("File", file_menu_items);

	// Re-activate File menu
	menu.close_menu();
	menu.handle_alt_key('f', queue);

	// Navigate to Open Recent... and press KEY_RIGHT
	menu.handle_key(KEY_DOWN, queue); // New File
	menu.handle_key(KEY_DOWN, queue); // Open...
	menu.handle_key(KEY_DOWN, queue); // Open Recent...
	menu.handle_key(KEY_RIGHT, queue); // Open submenu
	assert(menu.is_submenu_open());

	// Press ENTER on first recent file (/tmp/test_recent_beta.cpp)
	menu.handle_key('\n', queue);
	assert(!menu.is_open()); // Menu should close after selection

	auto pop_ev = queue.pop();
	assert(pop_ev.has_value());
	assert(pop_ev->type == event_type::open_file);
	assert(pop_ev->payload == "/tmp/test_recent_beta.cpp");

	std::cout << "menu_bar submenu and Open Recent tests passed successfully!\n";
	return 0;
}
