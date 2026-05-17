#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale.h>
#include <ncurses.h>
#include <string>
#include "CLI11.hpp"
#include "editor.h"
#include "event_logger.h"
#include "config_manager.h"

namespace fs = std::filesystem;

std::string get_home_dir()
{
	const char *home = getenv("HOME");
	if (home)
		return std::string(home);
	return ".";
}

int main(int argc, char **argv)
{
	CLI::App app{"Turbostar Editor"};

	std::string log_file;
	bool debug_mode = false;
	bool exit_immediately = false;
	std::string debug_string;
	std::vector<std::string> filenames;

	app.add_option("--log", log_file, "Path to log file");
	app.add_flag("--debug", debug_mode, "Enable debug mode");
	app.add_flag("--exit-immediately", exit_immediately, "Exit after 1 second");
	app.add_option("--debug-filter", debug_string, "Debug filter string");
	app.add_option("filenames", filenames, "Files to edit");
	app.set_version_flag("--version", TURBOSTAR_VERSION);

	std::string config_path = get_home_dir() + "/.turbostar";
	app.set_config("--config", config_path, "Read an ini file", false);

	// Ensure default config file exists
	// Policy: If ~/.turbostar does not exist, we write out a default one.
	// Currently we have no default configuration settings, but this establishes
	// the pattern for when we add them (e.g., tab width, theme).
	if (!fs::exists(config_path)) {
		try {
			std::ofstream default_config(config_path);
			if (default_config.is_open()) {
				default_config << "# Turbostar Configuration File\n";
				default_config << "# Currently no default settings are applied, but this file is ready for future options.\n";
			}
		} catch (...) {
			// Ignore errors creating default config
		}
	}

	CLI11_PARSE(app, argc, argv);

	config_manager::get_instance().load();

	auto &logger = event_logger::get_instance();
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
	// Note: using (COLOR_X + 8) to access bright versions (8-15) in
	// 16-color terminals
	init_pair(1, COLOR_BLACK, COLOR_WHITE); // Menu/Status bar
	init_pair(2, COLOR_RED, COLOR_WHITE);	// Hotkeys
	init_pair(3, COLOR_YELLOW + 8,
		  COLOR_BLUE);		      // Window Text (Bright Yellow on Blue)
	init_pair(4, COLOR_CYAN, COLOR_BLUE); // Scrollbars
	init_pair(5, COLOR_WHITE + 8,
		  COLOR_BLUE);			// Window borders/widgets (Bright White on Blue)
	init_pair(6, COLOR_BLACK, COLOR_BLACK); // Drop shadows
	init_pair(7, COLOR_RED, COLOR_BLACK);	// Hotkeys on selected background
	init_pair(8, COLOR_WHITE + 8,
		  COLOR_CYAN);			 // Selection highlight (Bright White)
	init_pair(9, COLOR_BLUE, COLOR_BLACK);	 // Desktop pattern (Darker)
	init_pair(10, COLOR_WHITE, COLOR_GREEN); // Buttons (White on Green)
	init_pair(11, COLOR_WHITE + 8,
		  COLOR_WHITE); // Dialog borders (Bright White on Gray)
	init_pair(16, COLOR_YELLOW + 8,
		  COLOR_WHITE);			     // Dialog Hotkeys (Bright Yellow on Gray)
	init_pair(12, COLOR_WHITE + 8, COLOR_BLUE);  // Syntax: Keyword
	init_pair(13, COLOR_YELLOW + 8, COLOR_CYAN); // Syntax: Selected Keyword
	init_pair(14, COLOR_BLACK, COLOR_GREEN);     // Selected Menu Item
	init_pair(15, COLOR_RED, COLOR_GREEN);	     // Hotkey on Selected Menu
	init_pair(17, COLOR_BLACK, COLOR_CYAN);	     // Dialog Group Box Content
	init_pair(18, COLOR_YELLOW + 8, COLOR_CYAN); // Dialog Group Box Hotkeys
	init_pair(19, COLOR_BLACK, COLOR_GREEN);     // Focused Widget
	init_pair(20, COLOR_GREEN + 8, COLOR_BLUE);  // Git Clean (Green on Blue)
	init_pair(21, COLOR_YELLOW + 8, COLOR_BLUE); // Git Dirty (Yellow on Blue)
	init_pair(22, COLOR_CYAN + 8, COLOR_BLUE);   // Syntax: Heading
	init_pair(23, COLOR_YELLOW + 8, COLOR_BLUE); // Syntax: Bold
	init_pair(24, COLOR_GREEN + 8, COLOR_BLUE);  // Syntax: List Item
	init_pair(25, COLOR_YELLOW + 8, COLOR_MAGENTA); // LSP Highlight (Normal)
	init_pair(26, COLOR_WHITE + 8, COLOR_MAGENTA);  // LSP Highlight (Keyword)
	init_pair(27, COLOR_WHITE + 8, COLOR_RED);      // LSP Error (White on Red)
	init_pair(28, COLOR_BLACK, COLOR_YELLOW);       // LSP Warning (Black on Yellow)

	if (can_change_color()) {
		// Red, Green, Blue values are on a scale of 0 to 1000
		init_color(COLOR_BLUE, 0, 0,
			   500); // Set to a deeper, darker navy blue
	}
	raw();

	nonl();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0); // Hide the cursor for now

	logger.log("UI initialized.");

	editor main_editor(debug_mode, debug_string, filenames, exit_immediately);
	main_editor.run();

	logger.log("Exiting application loop.");

	endwin();

	return 0;
}
