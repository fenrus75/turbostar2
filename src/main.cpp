#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale.h>
#include <ncurses.h>
#include <string>
#include "CLI11.hpp"
#include "agentlib/skill_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "project_manager.h"
#include "fs_utils.h"

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
	bool no_lsp = false;
	bool no_welcome = false;
	double exit_immediately = -1.0;
	std::string debug_string;
	std::vector<std::string> filenames;

	app.add_option("--log", log_file, "Path to log file");
	app.add_flag("--debug", debug_mode, "Enable debug mode");
	app.add_flag("--no-lsp", no_lsp, "Disable LSP functionality");
	app.add_flag("--no-welcome-screen", no_welcome, "Disable the welcome screen on startup");
	app.add_option("--exit-immediately", exit_immediately, "Exit after N seconds")->expected(0, 1)->default_str("1.0");
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
				default_config
				    << "# Currently no default settings are applied, but this file is ready for future options.\n";
			}
		} catch (...) {
			// Ignore errors creating default config
		}
	}

	CLI11_PARSE(app, argc, argv);

	config_manager::get_instance().load();
	agentlib::skill_manager::get_instance().initialize();

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
		  COLOR_WHITE);				// Dialog Hotkeys (Bright Yellow on Gray)
	init_pair(12, COLOR_WHITE + 8, COLOR_BLUE);	// Syntax: Keyword
	init_pair(13, COLOR_YELLOW + 8, COLOR_CYAN);	// Syntax: Selected Keyword
	init_pair(14, COLOR_BLACK, COLOR_GREEN);	// Selected Menu Item
	init_pair(15, COLOR_RED, COLOR_GREEN);		// Hotkey on Selected Menu
	init_pair(17, COLOR_BLACK, COLOR_CYAN);		// Dialog Group Box Content
	init_pair(18, COLOR_YELLOW + 8, COLOR_CYAN);	// Dialog Group Box Hotkeys
	init_pair(19, COLOR_BLACK, COLOR_GREEN);	// Focused Widget
	init_pair(20, COLOR_GREEN + 8, COLOR_BLUE);	// Git Clean (Green on Blue)
	init_pair(21, COLOR_YELLOW + 8, COLOR_BLUE);	// Git Dirty (Yellow on Blue)
	init_pair(22, COLOR_CYAN + 8, COLOR_BLUE);	// Syntax: Heading
	init_pair(23, COLOR_YELLOW + 8, COLOR_BLUE);	// Syntax: Bold
	init_pair(24, COLOR_GREEN + 8, COLOR_BLUE);	// Syntax: List Item
	init_pair(25, COLOR_YELLOW + 8, COLOR_MAGENTA); // LSP Highlight (Normal)
	init_pair(26, COLOR_WHITE + 8, COLOR_MAGENTA);	// LSP Highlight (Keyword)
	init_pair(27, COLOR_WHITE + 8, COLOR_RED);	// LSP Error (White on Red)
	init_pair(28, COLOR_BLACK, COLOR_YELLOW);	// LSP Warning (Black on Yellow)
	init_pair(29, COLOR_WHITE, COLOR_BLACK);	// Terminal Output (White on Black)
	init_pair(30, COLOR_GREEN + 8, COLOR_BLUE);	// Diff Add (Bright Green on Blue)
	init_pair(31, COLOR_RED + 8, COLOR_BLUE);	// Diff Delete (Bright Red on Blue)
	init_pair(32, COLOR_CYAN + 8, COLOR_BLUE);	// Diff Header (Bright Cyan on Blue)
	init_pair(33, COLOR_YELLOW + 8, COLOR_CYAN);	// Read Lines Pending (Bright Yellow on Cyan)
	init_pair(34, COLOR_GREEN + 8, COLOR_CYAN);	// Read Lines Success (Bright Green on Cyan)
	init_pair(35, COLOR_RED + 8, COLOR_CYAN);	// Read Lines Failure (Bright Red on Cyan)
	init_pair(36, COLOR_WHITE + 8, COLOR_BLACK);	// Terminal Border (Bright White on Black)
	init_pair(37, COLOR_BLACK + 8, COLOR_WHITE);	// Disabled Menu Item (Dark Gray on White)

	// Agent interaction pairs (50-59: Primary background - Light Blue)
	init_pair(50, COLOR_BLACK, COLOR_BLUE + 8);
	init_pair(51, COLOR_BLUE, COLOR_BLUE + 8);
	init_pair(52, COLOR_YELLOW + 8, COLOR_BLUE + 8);
	init_pair(53, COLOR_RED, COLOR_BLUE + 8);
	init_pair(54, COLOR_GREEN + 8, COLOR_BLUE + 8);
	init_pair(55, COLOR_RED + 8, COLOR_BLUE + 8);
	init_pair(56, COLOR_MAGENTA, COLOR_BLUE + 8);

	// Agent interaction pairs (60-69: Alternate background - Cyan)
	init_pair(60, COLOR_BLACK, COLOR_CYAN);
	init_pair(61, COLOR_BLUE, COLOR_CYAN);
	init_pair(62, COLOR_YELLOW + 8, COLOR_CYAN);
	init_pair(63, COLOR_RED, COLOR_CYAN);
	init_pair(64, COLOR_GREEN + 8, COLOR_CYAN);
	init_pair(65, COLOR_RED + 8, COLOR_CYAN);
	init_pair(66, COLOR_MAGENTA, COLOR_CYAN);

	// Agent interaction pairs (70-79: System background - White)
	init_pair(70, COLOR_BLACK, COLOR_WHITE);
	init_pair(71, COLOR_BLUE, COLOR_WHITE);
	init_pair(72, COLOR_YELLOW + 8, COLOR_WHITE);
	init_pair(73, COLOR_RED, COLOR_WHITE);
	init_pair(74, COLOR_GREEN + 8, COLOR_WHITE);
	init_pair(75, COLOR_RED + 8, COLOR_WHITE);
	init_pair(76, COLOR_MAGENTA, COLOR_WHITE);

	if (can_change_color()) {
		// Red, Green, Blue values are on a scale of 0 to 1000
		init_color(COLOR_BLUE, 0, 0,
			   500); // Set to a deeper, darker navy blue
	}
	raw();

	nonl();
	noecho();
	keypad(stdscr, TRUE);

	// Enable mouse tracking for clicks
	mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
	// Tell the terminal to send mouse events (works for some xterm-compatible terms)
	// Also enable bracketed paste mode
	printf("\033[?1000h\033[?2004h\n");
	fflush(stdout);

	curs_set(0); // Hide the cursor for now

	logger.log("UI initialized.");

	project_manager::get_instance().initialize();

	// Load project-specific configuration overlay if available
	std::string cache_root = fs_utils::get_project_cache_root();
	if (!cache_root.empty()) {
		std::string project_config_path = fs::path(cache_root) / "config.ini";
		if (fs::exists(project_config_path)) {
			config_manager::get_instance().load_from_file(project_config_path);
		}
	}

	editor main_editor(debug_mode, debug_string, filenames, exit_immediately, no_lsp, no_welcome);
	main_editor.run();

	logger.log("Exiting application loop.");
	project_manager::get_instance().shutdown();

	// Disable mouse tracking and bracketed paste mode
	printf("\033[?1000l\033[?2004l\n");
	fflush(stdout);

	endwin();

	logger.log("Application exiting main().");
	return 0;
}
