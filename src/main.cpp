#include <iostream>
#include <string>
#include <ncurses.h>
#include <locale.h>
#include "event_logger.h"
#include "menu_bar.h"
#include "status_bar.h"

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
	initscr();
	start_color();
	use_default_colors();
	// Color pairs based on docs/colorscheme.md
	init_pair(1, COLOR_BLACK, COLOR_WHITE); // Menu/Status bar
	init_pair(2, COLOR_RED, COLOR_WHITE);   // Hotkeys
	init_pair(3, COLOR_WHITE, COLOR_BLUE);  // Desktop/Window

	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0); // Hide the cursor for now

	menu_bar top_menu;
	status_bar bottom_status;

	// Paint desktop background
	attron(COLOR_PAIR(3));
	for (int y = 1; y < LINES - 1; ++y) {
		move(y, 0);
		for (int x = 0; x < COLS; ++x) {
			addch(' ');
		}
	}
	attroff(COLOR_PAIR(3));

	top_menu.draw();
	bottom_status.draw();
	refresh();

	logger.log("UI initialized.");

	int ch;
	while ((ch = getch()) != 'q') {
		logger.log("Key pressed: " + std::to_string(ch));
		
		std::string debug_out;
		if (debug_mode) {
			auto msg = logger.get_latest_matching_message(debug_string);
			if (msg) {
				debug_out = ">>" + *msg + "<<";
			}
		}
		
		bottom_status.draw(debug_out);
		refresh();
	}

	logger.log("Exiting application loop.");

	endwin();

	if (!log_file.empty()) {
		logger.write_to_file(log_file);
	}

	return 0;
}
