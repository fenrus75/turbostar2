#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "file_dialog.h"
#include "find_dialog.h"
#include "history_manager.h"
#include "config_manager.h"
#include "settings_dialog.h"
#include "git_manager.h"
#include "clangd_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include "agent_window.h"
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

namespace fs = std::filesystem;

void editor::dispatch_event_ui(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::force_quit) {
		logger.log("Dispatching force_quit event.");
		
		bool any_dirty = false;
		for (const auto& doc : documents_) {
			if (doc->is_modified()) {
				any_dirty = true;
				break;
			}
		}
		
		if (any_dirty) {
			active_dialog_ = std::make_unique<force_quit_dialog>();
			active_dialog_mode_ = dialog_mode::force_quit_prompt;
			set_focus(focus_target::dialog, "force_quit");
			return;
		}
		
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		
		// If no windows, just exit
		if (windows_.empty()) {
			is_running_ = false;
			return;
		}

		// Otherwise, attempt to close the active window.
		// Since closing the last window triggers a quit, this will cascade.
		// But wait, if they have 5 windows, quit should close ALL of them.
		// If we just push 5 close_window events?
		// Actually, let's just use the active window. The user can press quit again.
		// Wait, a standard quit closes everything.
		// If we just push a close_window event, and if it succeeds, push another quit?
		// Yes! 
		editor_event close_ev;
		close_ev.type = event_type::close_window;
		global_queue_.push(close_ev);
		
		if (windows_.size() > 1) {
			editor_event quit_ev;
			quit_ev.type = event_type::quit;
			global_queue_.push(quit_ev);
		}
		
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
		active_dialog_ = std::make_unique<message_dialog>("About TurboStar", about_lines);
		set_focus(focus_target::dialog, "menu_about");
		return;
	}

	if (ev.type == event_type::settings) {
		logger.log("Dispatching settings event.");
		active_dialog_ = std::make_unique<settings_dialog>();
		active_dialog_mode_ = dialog_mode::settings;
		set_focus(focus_target::dialog, "settings");
		return;
	}

	if (ev.type == event_type::open_agent) {
		logger.log("Dispatching open_agent event.");
		new_agent_window();
		return;
	}

	if (ev.type == event_type::agent_response) {
		logger.log("Dispatching agent_response event.");
		// Find the active agent window
		for (auto& win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window*>(win.get())) {
				agent_win->append_response(ev.payload);
				break;
			}
		}
		return;
	}

	if (ev.type == event_type::agent_tool_update) {
		logger.log("Dispatching agent_tool_update event.");
		// Find the active agent window
		for (auto& win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window*>(win.get())) {
				agent_win->append_tool_update(ev.payload);
				break;
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
			for (const auto& doc : documents_) {
				if (fs_utils::safe_absolute(doc->get_filename()).lexically_normal().string() == safe_path) {
					target_doc = doc;
					break;
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
}
