#include <iostream>
#include <string>
#include <ncurses.h>
#include <locale.h>
#include "event_logger.h"
#include "editor.h"

int main(int argc, char** argv)
{
	std::string log_file;
	bool debug_mode = false;
	std::string debug_string;
	std::string filename = "unknown.txt";

	// Basic argument parsing
	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--log" && i + 1 < argc) {
			log_file = argv[++i];
		} else if (arg == "--debug") {
			debug_mode = true;
			if (i + 1 < argc && argv[i + 1][0] != '-') {
				debug_string = argv[++i];
			}
		} else if (arg[0] != '-') {
			filename = arg;
		}
	}

	auto& logger = event_logger::get_instance();
	if (!log_file.empty()) {
		logger.set_log_file(log_file);
	}
	logger.log("Application started.");

	if (debug_mode) {
		logger.log("Debug mode enabled. Filter string: '" + debug_string + "'");
	}

	// Initialize ncurses
	setlocale(LC_ALL, ""); // Important for UTF-8 and ncursesw
	setenv("ESCDELAY", "25", 1);
	initscr();
	start_color();
	// Color pairs based on docs/colorscheme.md
	// Note: using (COLOR_X + 8) to access bright versions (8-15) in 16-color terminals
	init_pair(1, COLOR_BLACK, COLOR_WHITE); // Menu/Status bar
	init_pair(2, COLOR_RED, COLOR_WHITE);   // Hotkeys
	init_pair(3, COLOR_YELLOW + 8, COLOR_BLUE); // Window Text (Bright Yellow on Blue)
	init_pair(4, COLOR_CYAN, COLOR_BLUE);   // Scrollbars
	init_pair(5, COLOR_WHITE + 8, COLOR_BLUE);  // Window borders/widgets (Bright White on Blue)
	init_pair(6, COLOR_BLACK, COLOR_BLACK); // Drop shadows
	init_pair(7, COLOR_RED, COLOR_BLACK);   // Hotkeys on selected background
	init_pair(8, COLOR_WHITE + 8, COLOR_CYAN);  // Selection highlight (Bright White)
	init_pair(9, COLOR_BLUE, COLOR_BLACK);  // Desktop pattern (Darker)
	init_pair(10, COLOR_BLACK, COLOR_GREEN); // Buttons
	init_pair(11, COLOR_WHITE + 8, COLOR_WHITE); // Dialog borders (Bright White on Gray)
	init_pair(12, COLOR_WHITE + 8, COLOR_BLUE);  // Syntax: Keyword
	init_pair(13, COLOR_YELLOW + 8, COLOR_CYAN); // Syntax: Selected Keyword

	if (can_change_color()) {
	        // Red, Green, Blue values are on a scale of 0 to 1000
	        init_color(COLOR_BLUE, 0, 0, 500); // Set to a deeper, darker navy blue
	}
	raw();

	nonl();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0); // Hide the cursor for now

	logger.log("UI initialized.");

	editor main_editor(debug_mode, debug_string, filename);
	main_editor.run();

	logger.log("Exiting application loop.");

	endwin();

	return 0;
}
