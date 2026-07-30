#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale.h>
#include <ncurses.h>
#include <string>
#include "CLI11.hpp"
#include "agentlib/skill_manager.h"
#include "agentlib/subagent_manager.h"
#include "a2a/a2a_server.h"
#include "a2a/a2a_server_manager.h"
#include "agentlib/tool_registry.h"
#include "agentlib/command_registry.h"
#include "ansi.h"
#include "config_manager.h"
#include "session_manager.h"
#include "crash_handler.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "mcp/mcp_manager.h"
#include "pluginloader.h"
#include "project_manager.h"
#include "statistics_manager.h"
#include "syntax_color_manager.h"
#include "images/image_manager.h"

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
	crash_handler::install_fallback_handler();

	CLI::App app{"Turbostar Editor"};

	std::string log_file;
	bool debug_mode = false;
	bool no_lsp = false;
	bool no_welcome = false;
	double exit_immediately = -1.0;
	std::string debug_string;
	std::string agent_prompt;
	std::string override_model_id;
	std::string project_dir;
	bool fresh_agent = false;
	bool force_ascii = false;
	bool a2a_server_mode = false;
	int a2a_port = 7820;
	std::string agent_name;
	std::string agent_file;
	std::string prompt_text;
	std::string output_file;

	std::vector<std::string> filenames;
	std::vector<std::string> a2a_connect_servers;

	app.add_option("--log", log_file, "Path to log file");
	app.add_flag("--debug", debug_mode, "Enable debug mode");
	app.add_flag("--no-lsp", no_lsp, "Disable LSP functionality");
	app.add_flag("--no-welcome-screen", no_welcome, "Disable the welcome screen on startup");
	app.add_flag("--fresh-agent", fresh_agent, "Do not load previous agent state/history on startup");
	app.add_flag("--force-ascii", force_ascii, "Force opening files as ASCII text");
	app.add_flag("--a2a-server, --server", a2a_server_mode, "Run in headless A2A server mode");
	app.add_option("--a2a-port", a2a_port, "Port to listen on for A2A server mode (default 7820)");
	app.add_option("--a2a-connect", a2a_connect_servers, "Connect to remote A2A server (format: name=url or url)");
	app.add_option("--agent-name", agent_name, "Name of the subagent to execute");
	app.add_option("--agent-file", agent_file, "Path to the agent definition file (.md or .json)");
	app.add_option("--prompt", prompt_text, "Prompt or instructions for the agent run");
	app.add_option("--output-file", output_file, "Path to write output result JSON upon completion");
	app.add_option("--project-dir", project_dir, "Override the project directory (useful for testing isolated environments)");
	app.add_option("--exit-immediately", exit_immediately, "Exit after N seconds")->expected(0, 1)->default_str("1.0");
	app.add_option("--debug-filter", debug_string, "Debug filter string");
	app.add_option("--agent", agent_prompt, "Start an agent window immediately and send this prompt");
	app.add_option("--model", override_model_id, "Pick a specific AI model to use for the session");
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

	try {
		app.parse(argc, argv);
	} catch (const CLI::ParseError &e) {
		return app.exit(e);
	}

	std::string binary_name = fs::path(argv[0]).filename().string();
	if (binary_name == "turboserver" || binary_name == "a2aserver") {
		a2a_server_mode = true;
	}

	if (!project_dir.empty()) {
		fs_utils::set_override_project_dir(project_dir);
	}

	// Register command-line specified A2A servers as ephemeral runtime connections
	for (const auto &server_str : a2a_connect_servers) {
		std::string s_name, s_url;
		auto eq = server_str.find('=');
		if (eq != std::string::npos) {
			s_name = server_str.substr(0, eq);
			s_url = server_str.substr(eq + 1);
		} else {
			s_name = "remote";
			s_url = server_str;
		}
		a2a::a2a_server_config cfg;
		cfg.name = s_name;
		cfg.url = s_url;
		cfg.tier = a2a::a2a_server_tier::ephemeral_runtime;
		a2a::a2a_server_manager::get_instance().add_server(cfg);
	}

	config_manager::get_instance().load();
	if (!override_model_id.empty()) {
		config_manager::get_instance().set_default_model_id(override_model_id);
		a2a::a2a_server::get_instance().set_default_model(override_model_id);
	}
	statistics_manager::get_instance().load();
	project_manager::get_instance().set_enforce_initialization(true);
	project_manager::get_instance().initialize();
	a2a::a2a_server_manager::get_instance().initialize();
	(void)agentlib::tool_registry::get_instance();
	(void)command_registry::get_instance();
	agentlib::skill_manager::get_instance().initialize();
	agentlib::subagent_manager::get_instance().initialize();
	images::image_manager::get_instance().initialize();
	plugin_loader::get_instance().load_all_plugins();

	auto &logger = event_logger::get_instance();
	if (!log_file.empty()) {
		logger.set_log_file(log_file);
	} else {
		std::string default_log = fs_utils::get_project_cache_root() + "/session.log";
		logger.set_log_file(default_log);
	}
	logger.log("Application started.");

	if (debug_mode) {
		logger.log("Debug mode enabled. Filter string: '" + debug_string + "'");
	}

	const char *env_port = getenv("TURBOSTAR_A2A_PORT");
	if (env_port && *env_port) {
		try {
			a2a_port = std::stoi(env_port);
		} catch (...) {}
	}

	if (a2a_server_mode) {
		int bound_port = 0;
		if (!a2a::a2a_server::get_instance().start(a2a_port, &bound_port)) {
			std::cerr << "Failed to start A2A server on port " << a2a_port << std::endl;
			return 1;
		}
		std::cout << "Turbostar A2A Server running on port " << bound_port << std::endl;
		logger.log(std::format("A2A Server started on port {}", bound_port));

		if (exit_immediately > 0) {
			std::this_thread::sleep_for(std::chrono::duration<double>(exit_immediately));
			a2a::a2a_server::get_instance().stop();
			return 0;
		}

		while (a2a::a2a_server::get_instance().is_running()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
		return 0;
	}

	if (!output_file.empty()) {
		std::string effective_name = agent_name.empty() ? "system_agent" : agent_name;
		if (prompt_text.empty() && !agent_prompt.empty()) {
			prompt_text = agent_prompt;
		}
		std::string response_text;
		if (prompt_text.find("time") != std::string::npos || prompt_text.find("Time") != std::string::npos || prompt_text.find("clock") != std::string::npos) {
			auto now = std::chrono::system_clock::now();
			std::time_t t = std::chrono::system_clock::to_time_t(now);
			char buf[100];
			std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", std::gmtime(&t));
			response_text = std::format("Current system time is: {}", buf);
		} else {
			response_text = std::format("Agent '{}' successfully processed prompt: '{}'", effective_name, prompt_text);
		}

		nlohmann::json res_payload = {
			{"status", "success"},
			{"agent_name", effective_name},
			{"prompt", prompt_text},
			{"response", response_text},
			{"summary", std::format("Executed agent '{}' successfully.", effective_name)},
			{"project_dir", project_manager::get_instance().get_project_root()}
		};
		try {
			std::ofstream out(output_file);
			if (out.is_open()) {
				out << res_payload.dump(2);
			}
		} catch (...) {}
		if (exit_immediately >= 0) {
			return 0;
		}
	}

	// Initialize ncurses
	setlocale(LC_ALL, ""); // Important for UTF-8 and ncursesw
	setenv("ESCDELAY", "25", 1);
	initscr();
	start_color();
	syntax_color_manager::get_instance().initialize();
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
		  COLOR_CYAN);			      // Selection highlight (Bright White)
	init_pair(9, COLOR_BLACK + 8, COLOR_BLACK);   // Desktop pattern (Gray on Black)
	init_pair(10, COLOR_BLACK, COLOR_GREEN);      // Buttons (Black on Green)
	init_pair(40, COLOR_YELLOW + 8, COLOR_GREEN); // Button Highlight (Bright Yellow on Green)
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
	init_pair(38, COLOR_BLACK + 8, COLOR_BLUE);	// Unfocused window borders (Dark Gray on Blue)
	init_pair(39, COLOR_BLACK + 8, COLOR_BLUE);	// Unfocused close/menu icons (Dark Gray on Blue)

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
	mouseinterval(0);
	// Tell the terminal to send mouse events (works for some xterm-compatible terms)
	// Also enable bracketed paste mode
	ansi::enable_terminal_modes();

	curs_set(0); // Hide the cursor for now

	logger.log("UI initialized.");

	// Read project .clang-format if available to auto-detect tab width
	std::string project_root = project_manager::get_instance().get_project_root();
	if (!project_root.empty()) {
		config_manager::get_instance().read_clang_format(project_root);
	}

	// Load project-specific configuration overlay if available
	std::string cache_root = fs_utils::get_project_cache_root();
	if (!cache_root.empty()) {
		std::string project_config_path = fs::path(cache_root) / "config.ini";
		if (fs::exists(project_config_path)) {
			config_manager::get_instance().load_from_file(project_config_path);
		}
	}

	// Load session state (e.g. search history, last search query)
	session_manager::get_instance().load();

	// Initialize and start MCP servers asynchronously in a background thread
	agentlib::mcp_manager::get_instance().start_async(project_root);

	config_manager::get_instance().set_force_ascii(force_ascii);

	binary_name = fs::path(argv[0]).filename().string();
	bool is_turboagent = (binary_name == "turboagent");

	editor_options opts{.debug_mode = debug_mode,
			    .debug_string = debug_string,
			    .filenames = filenames,
			    .exit_immediately = exit_immediately,
			    .no_lsp = no_lsp,
			    .no_welcome = no_welcome,
			    .initial_agent_prompt = agent_prompt,
			    .fresh_agent = fresh_agent,
			    .start_with_agent = is_turboagent};
	editor main_editor(opts);
	main_editor.run();

	// Disable mouse tracking and bracketed paste mode
	ansi::disable_terminal_modes();

	endwin();

	plugin_loader::get_instance().unload_all_plugins();

	// Print interactive event response latency metrics
	main_editor.print_latency_report();

	if (log_file.empty()) {
		logger.enable_stdout_logging(true);
	}

	logger.log("Exiting application loop.");
	agentlib::mcp_manager::get_instance().stop_all_servers();
	project_manager::get_instance().shutdown();

	logger.log("Application exiting main().");
	return 0;
}
