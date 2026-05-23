#include "ui/dialog_factories.h"
#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "history_manager.h"
#include "config_manager.h"
#include "git_manager.h"
#include "lsp_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include "ui/agent_window.h"
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
			active_dialog_ = create_force_quit_dialog();
			active_dialog_mode_ = dialog_mode::force_quit_prompt;
			set_focus(focus_target::dialog, "force_quit");
			return;
		}
		
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_quitting_ = true;
		
		// If no windows, just exit
		if (windows_.empty()) {
			is_running_ = false;
			return;
		}

		// Check if any documents are modified. If so, prompt for the first modified document.
		for (const auto& doc : documents_) {
			if (doc && doc->is_modified()) {
				std::string fname = doc->get_filename();
				if (fname.empty()) fname = "untitled.txt";
				
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
		active_dialog_ = create_settings_dialog();
		active_dialog_mode_ = dialog_mode::settings;
		set_focus(focus_target::dialog, "settings");
		return;
	}

	if (ev.type == event_type::open_agent) {
		logger.log("Dispatching open_agent event.");
		new_agent_window();
		return;
	}

	if (ev.type == event_type::open_coredump_viewer) {
		logger.log("Dispatching open_coredump_viewer event.");
		new_coredump_window();
		return;
	}

	if (ev.type == event_type::open_subagent) {
		logger.log("Dispatching open_subagent event for agent ID: " + std::to_string(ev.key_code));
		
		int target_id = ev.key_code;
		
		// 1. Check if an agent window for this subagent already exists
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (auto aw = dynamic_cast<agent_window*>(windows_[i].get())) {
				if (aw->get_agent()->get_id() == target_id) {
					activate_window(i);
					return;
				}
			}
		}
		
		// 2. Window doesn't exist, we need to find the subagent and spawn a window
		std::shared_ptr<agentlib::ai_agent> found_subagent = nullptr;
		for (auto& win : windows_) {
			// Find a status window or main agent window that knows about this subagent
			if (auto aw = dynamic_cast<agent_window*>(win.get())) {
				for (auto& sub : aw->get_agent()->get_subagents()) {
					if (sub->get_id() == target_id) {
						found_subagent = sub;
						break;
					}
				}
			}
			if (found_subagent) break;
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
		for (auto& win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window*>(win.get())) {
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
		// Find the active agent window
		for (auto& win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window*>(win.get())) {
				if (agent_win->get_agent()->get_id() == ev.key_code) {
					agent_win->on_agent_update();
					break;
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

	if (ev.type == event_type::prompt_user) {
		logger.log("Dispatching prompt_user event.");
		active_ask_user_promise_ = ev.prompt_promise;
		active_dialog_ = create_ask_user_dialog(ev.payload, ev.prompt_options);
		active_dialog_mode_ = dialog_mode::ask_user;
		set_focus(focus_target::dialog, "prompt_user");
		return;
	}

	if (ev.type == event_type::agent_response) {
		// Clean up headless agent if it exists
		headless_agents_.erase(std::remove_if(headless_agents_.begin(), headless_agents_.end(),
			[&ev](const std::shared_ptr<agentlib::ai_agent>& agent) {
				return agent->get_id() == ev.key_code;
			}), headless_agents_.end());
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
