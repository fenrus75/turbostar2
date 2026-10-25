// Tested source file: src/ui/inspector_window.cpp
#include <cassert>
#include <iostream>
#include <ncurses.h>
#include "ui/inspector_window.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	// Initialize dummy curses terminal for drawing tests
	initscr();
	start_color();

	std::cout << "Testing inspector_window initialization..." << std::endl;
	inspector_window insp(1, 0, 10, 80, 5);
	assert(insp.get_id() == 1);
	assert(insp.get_x() == 0);
	assert(insp.get_y() == 10);
	assert(insp.get_width() == 80);
	assert(insp.get_height() == 5);
	assert(insp.get_state() == inspector_state::idle);
	assert(insp.get_token().empty());
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	assert(insp.bypass_global_shortcuts() == true);

	std::cout << "Testing draw_content in idle state..." << std::endl;
	insp.draw_content(false);
	insp.draw_content(true);

	std::cout << "Testing set_resolving..." << std::endl;
	insp.set_resolving("calculate_hash");
	assert(insp.get_state() == inspector_state::resolving);
	assert(insp.get_token() == "calculate_hash");
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	insp.draw_content(false);

	std::cout << "Testing set_active_info..." << std::endl;
	std::vector<turbostar::call_suggestion> proposals = {
		{"(\"data\", 4)", "(const char *data, size_t len)", 0, false},
		{"(sv)", "(std::string_view sv)", 0, false}
	};
	insp.set_active_info("calculate_hash", "uint64_t calculate_hash(const char *data, size_t len)", proposals, "src/hash.h:15");
	assert(insp.get_state() == inspector_state::active);
	assert(insp.get_token() == "calculate_hash");
	assert(insp.get_signature() == "uint64_t calculate_hash(const char *data, size_t len)");
	assert(insp.get_proposals().size() == 2);
	assert(insp.get_proposals()[0].call_text == "(\"data\", 4)");
	assert(insp.get_location() == "src/hash.h:15");
	insp.draw_content(false);

	std::cout << "Testing set_blanked..." << std::endl;
	insp.set_blanked();
	assert(insp.get_state() == inspector_state::blanked);
	insp.draw_content(false);

	std::cout << "Testing set_idle..." << std::endl;
	insp.set_idle();
	assert(insp.get_state() == inspector_state::idle);
	assert(insp.get_token().empty());
	assert(insp.get_signature().empty());
	assert(insp.get_proposals().empty());
	assert(insp.get_location().empty());
	insp.draw_content(false);

	std::cout << "Testing bounds resizing..." << std::endl;
	insp.set_bounds(0, 15, 100, 6);
	assert(insp.get_x() == 0);
	assert(insp.get_y() == 15);
	assert(insp.get_width() == 100);
	assert(insp.get_height() == 6);

	endwin();
	std::cout << "inspector_window tests passed successfully!" << std::endl;
	return 0;
}
