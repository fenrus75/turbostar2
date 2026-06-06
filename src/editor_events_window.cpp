#include <algorithm>
#include <chrono>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "ui/dialog_factories.h"
#include "ui/agent_window.h"

namespace fs = std::filesystem;

void editor::dispatch_event_window(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::close_window) {
		logger.log("Dispatching close_window event.");
		size_t active_idx = static_cast<size_t>(-1);
		for (size_t i = 0; i < windows_.size(); ++i) {
			if (windows_[i]->is_active()) {
				active_idx = i;
				break;
			}
		}

		if (active_idx != static_cast<size_t>(-1)) {
			std::shared_ptr<document> doc = windows_[active_idx]->get_document();

			// Check if we need to prompt for save
			if (doc && doc->is_modified() && !doc->is_read_only() && ev.key_code != 1) {
				// Don't prompt to save internal/special buffers
				std::string title = windows_[active_idx]->get_title();
				if (title != "Agent Chat" && title != "Compile Output" && title != "Test Output") {
					int doc_users = 0;
					for (const auto &w : windows_) {
						if (w->get_document() == doc)
							doc_users++;
					}
					if (doc_users == 1) {
						// This is the last window for a modified document. Prompt!
						std::string fname = doc->get_filename();
						if (fname.empty())
							fname = "untitled.txt";
						active_dialog_ = create_save_prompt_dialog(fname);
						active_dialog_mode_ = dialog_mode::save_prompt;
						set_focus(focus_target::dialog, "close_window");
						return; // Stop the close process until dialog resolves
					}
				}
			}

			// If it's a build process window, stop the process
			if (current_build_process_ &&
			    (windows_[active_idx]->get_title() == "Compile Output" || windows_[active_idx]->get_title() == "Test Output")) {
				current_build_process_->stop();
			}

			if (doc && !doc->get_filename().empty() && doc->has_nondefault_filename() && !doc->is_read_only()) {
				history_manager::get_instance().set_cursor_pos(doc->get_filename(), doc->get_cursor_x(),
									       doc->get_cursor_y());
			}

			windows_.erase(windows_.begin() + active_idx);

			// Remove document if no other window is using it
			bool doc_used = false;
			for (const auto &w : windows_) {
				if (w->get_document() == doc) {
					doc_used = true;
					break;
				}
			}
			if (!doc_used) {
				for (auto it = documents_.begin(); it != documents_.end(); ++it) {
					if (*it == doc) {
						documents_.erase(it);
						break;
					}
				}
			}

			if (windows_.empty()) {
				editor_event quit_ev;
				quit_ev.type = event_type::quit;
				global_queue_.push(quit_ev);
			} else {
				size_t next_idx = (active_idx == 0) ? 0 : active_idx - 1;
				if (next_idx >= windows_.size())
					next_idx = windows_.size() - 1;
				activate_window(next_idx);
			}
		}
		return;
	}

	if (ev.type == event_type::select_window) {
		logger.log("Selecting window: " + std::to_string(ev.key_code));
		activate_window(static_cast<size_t>(ev.key_code));
		return;
	}

	if (ev.type == event_type::maximize_window) {
		logger.log("Dispatching maximize_window event.");
		window *w = get_active_window();
		if (w) {
			if (w->is_maximized()) {
				w->set_maximized(false);
				w->set_bounds(w->get_restore_x(), w->get_restore_y(), w->get_restore_width(), w->get_restore_height());
			} else {
				w->save_restore_bounds();
				w->set_maximized(true);
				w->set_bounds(0, 1, COLS, LINES - 2);
			}
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return;
	}

	if (ev.type == event_type::agent_hide_sidebar) {
		logger.log("Dispatching agent_hide_sidebar event.");
		window *w = get_active_window();
		if (w) {
			if (auto aw = dynamic_cast<agent_window *>(w)) {
				aw->set_sidebar_expanded(false);
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
			}
		}
		return;
	}

	if (ev.type == event_type::agent_show_sidebar) {
		logger.log("Dispatching agent_show_sidebar event.");
		window *w = get_active_window();
		if (w) {
			if (auto aw = dynamic_cast<agent_window *>(w)) {
				aw->set_sidebar_expanded(true);
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
			}
		}
		return;
	}
}
