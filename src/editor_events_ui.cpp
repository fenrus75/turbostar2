#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cstdlib>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "build_error_manager.h"
#include "command_runner.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "git_manager.h"
#include "help_text.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "ui/agent_status_window.h"
#include "ui/agent_window.h"
#include "ui/dialog_factories.h"
#include "ui/terminal_window.h"

namespace fs = std::filesystem;

static int find_free_port()
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return 1234;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
		socklen_t len = sizeof(addr);
		if (getsockname(sock, (struct sockaddr *)&addr, &len) == 0) {
			int port = ntohs(addr.sin_port);
			close(sock);
			return port;
		}
	}
	close(sock);
	return 1234;
}

void editor::dispatch_event_ui(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::force_quit) {
		logger.log("Dispatching force_quit event.");

		bool any_dirty = false;
		for (const auto &doc : documents_) {
			if (doc->is_modified()) {
				any_dirty = true;
				break;
			}
		}

		if (any_dirty) {
			active_dialog_ = create_force_quit_dialog();
			active_dialog_mode_ = dialog_mode::force_quit_prompt;
			set_focus(focus_target::dialog, "force_quit");
			return;
		}

		logger.log("Application exit requested (source: force_quit clean).");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_quitting_ = true;

		// If no windows, just exit
		if (windows_.empty()) {
			logger.log("Application exit requested (source: quit clean, no windows).");
			is_running_ = false;
			return;
		}

		// Check if any documents are modified. If so, prompt for the first modified document.
		for (const auto &doc : documents_) {
			if (doc && doc->is_modified() && !doc->is_read_only()) {
				std::string fname = doc->get_filename();
				if (fname.empty())
					fname = "untitled.txt";

				// Make sure we only prompt if we aren't already prompting
				if (active_dialog_mode_ != dialog_mode::save_prompt) {
					active_dialog_ = create_save_prompt_dialog(fname);
					active_dialog_mode_ = dialog_mode::save_prompt;

					// Important: pass a flag in the dialog payload so we know we are in a quit loop
					set_focus(focus_target::dialog, "quit_all");
				}
				return;
			}
		}

		// If no documents are modified (or they have been saved/discarded), it is safe to exit.
		logger.log("Application exit requested (source: quit clean, no dirty docs).");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::redraw) {
		logger.log("Dispatching redraw event.");
		return;
	}

	if (ev.type == event_type::about) {
		logger.log("Dispatching about event.");
		std::vector<std::string> about_lines = {"TurboStar Editor",   "Version 0.1.0",	 "", "A nostalgia inspired TUI editor", "",
							"Copyright (c) 2026", "Arjan van de Ven"};
		active_dialog_ = create_message_dialog("About TurboStar", about_lines);
		set_focus(focus_target::dialog, "menu_about");
		return;
	}

	if (ev.type == event_type::help) {
		logger.log("Dispatching help event.");
		// Check if "Help" window already exists
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->get_title() == "Help") {
				activate_window(i);
				return;
			}
		}

		auto doc = std::make_shared<document>(global_queue_, "Help");

		std::stringstream ss(reinterpret_cast<const char *>(help_text_md));
		std::string line;
		while (std::getline(ss, line)) {
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			doc->append_line(line);
		}

		doc->set_read_only(true);
		documents_.push_back(doc);

		auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, "Help");
		win->attach_document(doc);
		windows_.push_back(std::move(win));
		activate_window(windows_.size() - 1);
		return;
	}

	if (ev.type == event_type::settings) {
		logger.log("Dispatching settings event.");
		active_dialog_ = create_settings_dialog();
		active_dialog_mode_ = dialog_mode::settings;
		set_focus(focus_target::dialog, "settings");
		return;
	}

	if (ev.type == event_type::run_settings) {
		logger.log("Dispatching run_settings event.");
		active_dialog_ = create_run_settings_dialog();
		active_dialog_mode_ = dialog_mode::run_settings;
		set_focus(focus_target::dialog, "run_settings");
		return;
	}

	if (ev.type == event_type::run_in_debugger) {
		logger.log("Dispatching run_in_debugger event.");
		std::string args = config_manager::get_instance().get_run_arguments();
		start_app(args, true);
		return;
	}

	if (ev.type == event_type::run_program) {
		logger.log("Dispatching run_program event.");
		std::string exe = config_manager::get_instance().get_main_executable();
		if (exe.empty()) {
			logger.log("run_program ignored: no main executable configured.");
			return;
		}

		std::string repo_root = git_manager::get_instance().get_repository_root();
		if (repo_root.empty()) {
			repo_root = std::filesystem::current_path().string();
		}
		std::filesystem::path build_exe = std::filesystem::path(repo_root) / "build" / exe;
		if (!std::filesystem::exists(build_exe)) {
			build_exe = std::filesystem::path(repo_root) / exe;
			if (!std::filesystem::exists(build_exe)) {
				build_exe = exe;
			}
		}

		std::string args = config_manager::get_instance().get_run_arguments();

		std::string run_mode = config_manager::get_instance().get_run_target_mode();
		if (run_mode == "window") {
			start_app(args, false);
			return;
		}

		// Full screen mode fallback
		logger.log("Running main executable full screen: " + build_exe.string());

		// Configure command_runner to generate the sandboxed command line
		sync_command_runner runner;
		runner.apply_build_profile();
		runner.set_use_pty(true);
		runner.set_enable_crash_catcher(true);

		std::string raw_cmd = build_exe.string();
		if (!args.empty()) {
			raw_cmd += " " + args;
		}

		std::string sandboxed_cmd = runner.build_command(raw_cmd);
		logger.log("Executing sandboxed command: " + sandboxed_cmd);

		// 1. Temporarily pause ncurses
		def_prog_mode();
		endwin();

		// 2. Execute the sandboxed process via std::system
		int status = std::system(sandboxed_cmd.c_str());
		(void)status;

		// 3. User confirmation keypress
		std::printf("\n\r[Process completed. Press any key to return to editor...]");
		std::fflush(stdout);

		struct termios old_tio, new_tio;
		bool termios_ok = (tcgetattr(STDIN_FILENO, &old_tio) == 0);
		if (termios_ok) {
			new_tio = old_tio;
			new_tio.c_lflag &= ~(ICANON | ECHO);
			new_tio.c_cc[VMIN] = 1;
			new_tio.c_cc[VTIME] = 0;
			tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
		}

		char dummy;
		int r = read(STDIN_FILENO, &dummy, 1);
		(void)r;

		if (termios_ok) {
			tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
		}

		// 4. Resume curses
		reset_prog_mode();
		refresh();
		clear();

		// Trigger editor redraw
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return;
	}

	if (ev.type == event_type::models_config) {
		logger.log("Dispatching models_config event.");
		active_dialog_ = create_model_list_dialog();
		active_dialog_mode_ = dialog_mode::model_list;
		set_focus(focus_target::dialog, "model_list");
		return;
	}

	if (ev.type == event_type::agent_switch_model) {
		int target_id = ev.key_code;
		if (target_id == 0) {
			window *active_win = get_active_window();
			if (dynamic_cast<agent_window *>(active_win)) {
				target_id = active_win->get_id();
			}
		}

		if (target_id != 0) {
			logger.log("Dispatching agent_switch_model event for agent ID " + std::to_string(target_id));
			switching_agent_id_ = target_id;
			active_dialog_ = create_model_selection_dialog();
			active_dialog_mode_ = dialog_mode::model_selection;
			set_focus(focus_target::dialog, "model_selection");
		} else {
			logger.log("agent_switch_model ignored: no active agent window.");
		}
		return;
	}

	if (ev.type == event_type::open_agent) {
		logger.log("Dispatching open_agent event.");
		new_agent_window();
		return;
	}

	if (ev.type == event_type::agent_save_history) {
		logger.log("Dispatching agent_save_history event.");
		std::shared_ptr<agentlib::ai_agent> active_agent = nullptr;
		if (current_focus_ == focus_target::window || current_focus_ == focus_target::popup) {
			if (auto aw = dynamic_cast<agent_window *>(get_active_window())) {
				active_agent = aw->get_agent();
			}
		}
		if (active_agent) {
			std::string filepath = "tmp/agent_chat_" + std::to_string(active_agent->get_id()) + ".json";
			active_agent->save_conversation(filepath);
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Saved conversation to " + filepath;
			global_queue_.push(status_ev);
		} else {
			editor_event status_ev;
			status_ev.type = event_type::set_transient_status;
			status_ev.payload = "Error: No active agent window.";
			global_queue_.push(status_ev);
		}
		return;
	}

	if (ev.type == event_type::open_crashdump_viewer) {
		logger.log("Dispatching open_crashdump_viewer event.");
		new_crashdump_window();
		return;
	}

	if (ev.type == event_type::open_subagent) {
		logger.log("Dispatching open_subagent event for agent ID: " + std::to_string(ev.key_code));

		int target_id = ev.key_code;

		// 1. Check if an agent window for this subagent already exists
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (auto aw = dynamic_cast<agent_window *>(windows_[i].get())) {
				if (aw->get_agent()->get_id() == target_id) {
					activate_window(i);
					return;
				}
			}
		}

		// 2. Window doesn't exist, we need to find the subagent and spawn a window
		std::shared_ptr<agentlib::ai_agent> found_subagent = nullptr;
		for (auto &win : windows_) {
			// Find a status window or main agent window that knows about this subagent
			if (auto aw = dynamic_cast<agent_window *>(win.get())) {
				for (auto &sub : aw->get_agent()->get_subagents()) {
					if (sub->get_id() == target_id) {
						found_subagent = sub;
						break;
					}
				}
			}
			if (found_subagent)
				break;
		}

		if (found_subagent) {
			open_subagent_window(found_subagent);
		} else {
			logger.log("Error: Could not find subagent with ID " + std::to_string(target_id) + " to open.");
		}
		return;
	}

	if (ev.type == event_type::agent_response) {
		logger.log("Dispatching agent_response event.");
		// Find the active agent window
		for (auto &win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
				if (agent_win->get_agent()->get_id() == ev.key_code) {
					agent_win->on_agent_update();
					break;
				}
			}
		}
		return;
	}

	if (ev.type == event_type::agent_tool_update) {
		logger.log("Dispatching agent_tool_update event.");
		// Find the active agent window and agent status window
		for (auto &win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
				if (agent_win->get_agent()->get_id() == ev.key_code) {
					agent_win->on_agent_update();
				}
			} else if (auto status_win = dynamic_cast<agent_status_window *>(win.get())) {
				if (status_win->get_agent() && status_win->get_agent()->get_id() == ev.key_code) {
					status_win->invalidate();
				}
			}
		}
		return;
	}

	if (ev.type == event_type::apply_edits) {
		logger.log("Dispatching apply_edits event.");
		// Payload is safe_path + "\n" + json
		size_t pos = ev.payload.find('\n');
		if (pos != std::string::npos) {
			std::string safe_path = ev.payload.substr(0, pos);
			std::string json_str = ev.payload.substr(pos + 1);

			// Find the document
			std::shared_ptr<document> target_doc = nullptr;
			for (const auto &doc : documents_) {
				std::string doc_path = doc->get_filename();
				if (!doc_path.empty()) {
					try {
						if (std::filesystem::weakly_canonical(doc_path).string() == safe_path) {
							target_doc = doc;
							break;
						}
					} catch (...) {
					}
				}
			}

			if (target_doc) {
				try {
					auto j = nlohmann::json::parse(json_str);
					if (j.is_array()) {
						target_doc->apply_external_edits_json(json_str);
					}
				} catch (...) {
					logger.log("Error parsing apply_edits json.");
				}
			}
		}
		return;
	}

	if (ev.type == event_type::prompt_user) {
		logger.log("Dispatching prompt_user event.");
		active_ask_user_promise_ = ev.prompt_promise;
		active_dialog_ = create_ask_user_dialog(ev.payload, ev.prompt_options);
		active_dialog_mode_ = dialog_mode::ask_user;
		set_focus(focus_target::dialog, "prompt_user");
		return;
	}

	if (ev.type == event_type::approve_plan) {
		logger.log("Dispatching approve_plan event.");
		active_ask_user_promise_ = ev.prompt_promise;
		active_dialog_ = create_plan_approval_dialog(ev.payload);
		active_dialog_mode_ = dialog_mode::approve_plan;
		set_focus(focus_target::dialog, "approve_plan");
		return;
	}

	if (ev.type == event_type::agent_response) {
		// Clean up headless agent if it exists
		headless_agents_.erase(
		    std::remove_if(headless_agents_.begin(), headless_agents_.end(),
				   [&ev](const std::shared_ptr<agentlib::ai_agent> &agent) { return agent->get_id() == ev.key_code; }),
		    headless_agents_.end());
		return;
	}

	if (ev.type == event_type::set_transient_status) {
		transient_status_message_ = ev.payload;
		transient_status_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		return;
	}

	if (ev.type == event_type::inline_agent_request) {
		launch_inline_agent(ev.payload);
		return;
	}
}

agentlib::start_app_result editor::start_app(const std::string &args, bool use_debugger)
{
	auto &logger = event_logger::get_instance();
	logger.log("start_app called with args: '" + args + "', debugger: " + (use_debugger ? "true" : "false"));

	std::string exe = config_manager::get_instance().get_main_executable();
	if (exe.empty()) {
		logger.log("start_app failed: no main executable configured.");
		return {-1, -1};
	}

	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (repo_root.empty()) {
		repo_root = std::filesystem::current_path().string();
	}
	std::filesystem::path build_exe = std::filesystem::path(repo_root) / "build" / exe;
	if (!std::filesystem::exists(build_exe)) {
		build_exe = std::filesystem::path(repo_root) / exe;
		if (!std::filesystem::exists(build_exe)) {
			build_exe = exe;
		}
	}

	// Clean up any existing terminal windows
	for (auto it = windows_.begin(); it != windows_.end();) {
		if ((*it)->get_title() == "Run Output" || (*it)->get_title() == "Debugger (GDB)") {
			if (auto tw = dynamic_cast<ui::terminal_window *>(it->get())) {
				tw->stop_process();
			}
			it = windows_.erase(it);
		} else {
			++it;
		}
	}

	agentlib::start_app_result result;

	if (use_debugger) {
		int port = find_free_port();
		logger.log("Found free port for GDBServer: " + std::to_string(port));

		int total_h = LINES - 2;
		int app_h = (total_h * 2) / 3;
		int gdb_h = total_h - app_h;

		int app_id = 1000 + static_cast<int>(windows_.size());
		int gdb_id = 1001 + static_cast<int>(windows_.size());

		auto app_tw = std::make_unique<ui::terminal_window>(app_id, 0, 1, COLS, app_h, "Run Output");
		app_tw->set_display_priority(10);
		auto gdb_tw = std::make_unique<ui::terminal_window>(gdb_id, 0, 1 + app_h, COLS, gdb_h, "Debugger (GDB)");
		gdb_tw->set_display_priority(10);

		std::string turbostar_exe;
		try {
			turbostar_exe = std::filesystem::read_symlink("/proc/self/exe").string();
		} catch (...) {
			turbostar_exe = "./turbostar";
		}
		std::string gdbserver_cmd = "exec gdbserver --wrapper " + turbostar_exe + " --gdb-wrapper -- localhost:" + std::to_string(port) + " " + build_exe.string();
		if (!args.empty()) {
			gdbserver_cmd += " " + args;
		}
		gdbserver_cmd += " 2>/dev/null";

		logger.log("Starting gdbserver: " + gdbserver_cmd);
		if (!app_tw->start_process(gdbserver_cmd, nullptr, true, false)) {
			logger.log("Failed to start gdbserver process.");
			return {-1, -1};
		}

		usleep(50000);

		std::string gdb_cmd = "exec gdb -q -ex \"set pagination off\" -ex \"target remote localhost:" + std::to_string(port) + "\"";
		if (config_manager::get_instance().get_gdb_auto_continue()) {
			gdb_cmd += " -ex \"continue\"";
		}
		gdb_cmd += " " + build_exe.string();

		logger.log("Starting gdb: " + gdb_cmd);
		if (!gdb_tw->start_process(gdb_cmd, nullptr, true, false)) {
			logger.log("Failed to start gdb process.");
			app_tw->stop_process();
			return {-1, -1};
		}

		ui::terminal_window *gdb_tw_ptr = gdb_tw.get();

		result.app_run_id = app_id;
		result.gdb_run_id = gdb_id;

		windows_.push_back(std::move(app_tw));
		windows_.push_back(std::move(gdb_tw));

		update_window_layout();

		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i].get() == gdb_tw_ptr) {
				activate_window(i);
				break;
			}
		}
		set_focus(focus_target::window, "start_app");
	} else {
		int app_id = 1000 + static_cast<int>(windows_.size());
		auto tw = std::make_unique<ui::terminal_window>(app_id, 0, 1, COLS, LINES - 2, "Run Output");
		tw->set_display_priority(10);

		std::string raw_cmd = build_exe.string();
		if (!args.empty()) {
			raw_cmd += " " + args;
		}

		logger.log("Starting app: " + raw_cmd);
		if (!tw->start_process(raw_cmd)) {
			logger.log("Failed to start app process.");
			return {-1, -1};
		}

		result.app_run_id = app_id;
		result.gdb_run_id = -1;

		windows_.push_back(std::move(tw));
		update_window_layout();
		activate_window(windows_.size() - 1);
		set_focus(focus_target::window, "start_app");
	}

	return result;
}

ui::terminal_window *editor::find_terminal_window(int run_id)
{
	for (auto &win : windows_) {
		if (win->get_id() == run_id) {
			return dynamic_cast<ui::terminal_window *>(win.get());
		}
	}
	return nullptr;
}

bool editor::write_to_run(int run_id, const std::string &data)
{
	auto *tw = find_terminal_window(run_id);
	if (!tw)
		return false;
	int fd = tw->get_pty_master_fd();
	if (fd < 0 || !tw->is_alive())
		return false;
	ssize_t w = write(fd, data.data(), data.size());
	return (w == static_cast<ssize_t>(data.size()));
}

agentlib::run_screenshot_data editor::get_run_screenshot(int run_id)
{
	auto *tw = find_terminal_window(run_id);
	if (!tw)
		return {};
	auto snap = tw->get_screenshot();
	agentlib::run_screenshot_data res;
	res.grid = snap.grid;
	res.cursor_x = snap.cursor_x;
	res.cursor_y = snap.cursor_y;
	res.cursor_visible = snap.cursor_visible;
	return res;
}

bool editor::terminate_run(int run_id)
{
	for (auto it = windows_.begin(); it != windows_.end(); ++it) {
		if ((*it)->get_id() == run_id) {
			if (auto tw = dynamic_cast<ui::terminal_window *>(it->get())) {
				tw->stop_process();
			}
			windows_.erase(it);
			update_window_layout();
			return true;
		}
	}
	return false;
}
