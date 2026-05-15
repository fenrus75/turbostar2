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
		}
	}

	auto& logger = event_logger::get_instance();
	logger.log("Application started.");
	
	if (debug_mode) {
		logger.log("Debug mode enabled. Filter string: '" + debug_string + "'");
	}

	// Initialize ncurses
	setlocale(LC_ALL, ""); // Important for UTF-8 and ncursesw
	setenv("ESCDELAY", "25", 1);
	initscr();
	start_color();
	use_default_colors();
	// Color pairs based on docs/colorscheme.md
	init_pair(1, COLOR_BLACK, COLOR_WHITE); // Menu/Status bar
	init_pair(2, COLOR_RED, COLOR_WHITE);   // Hotkeys
	init_pair(3, COLOR_WHITE, COLOR_BLUE);  // Desktop/Window
	init_pair(4, COLOR_CYAN, COLOR_BLUE);   // Scrollbars
	init_pair(5, COLOR_GREEN, COLOR_BLUE);  // Window widgets

	raw();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0); // Hide the cursor for now

	logger.log("UI initialized.");

	editor main_editor(debug_mode, debug_string);
	main_editor.run();

	logger.log("Exiting application loop.");

	endwin();

	if (!log_file.empty()) {
		logger.write_to_file(log_file);
	}

	return 0;
}
