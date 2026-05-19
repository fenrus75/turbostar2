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
#include "help_text.h"
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

		// Check if any documents are modified. If so, prompt for the first modified document.
		for (const auto& doc : documents_) {
			if (doc->is_modified()) {
				std::string fname = doc->get_filename();
				if (fname.empty()) fname = "untitled.txt";
				
				// Make sure we only prompt if we aren't already prompting
				if (active_dialog_mode_ != dialog_mode::save_prompt) {
					active_dialog_ = std::make_unique<save_prompt_dialog>(fname);
					active_dialog_mode_ = dialog_mode::save_prompt;
					
					// Important: pass a flag in the dialog payload so we know we are in a quit loop
					set_focus(focus_target::dialog, "quit_all");
				}
				return;
			}
		}

		// If no documents are modified (or they have been saved/discarded), it is safe to exit.
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
		active_dialog_ = std::make_unique<message_dialog>("About TurboStar", about_lines);
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
		
		std::stringstream ss(reinterpret_cast<const char*>(help_text_md));
		std::string line;
		while (std::getline(ss, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
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
				std::string doc_path = doc->get_filename();
				if (!doc_path.empty()) {
					try {
						if (std::filesystem::weakly_canonical(doc_path).string() == safe_path) {
							target_doc = doc;
							break;
						}
					} catch (...) {}
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
