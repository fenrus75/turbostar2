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
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

namespace fs = std::filesystem;

void editor::dispatch_event_mouse(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::mouse_click) {
		logger.log("Dispatching mouse click at X=" + std::to_string(ev.mouse_x) + " Y=" + std::to_string(ev.mouse_y));

		if (active_dialog_) {
			auto res = active_dialog_->handle_mouse(ev.mouse_x, ev.mouse_y);
			if (res.has_value()) {
				dialog_result dres = res.value();
				if (dres != dialog_result::pending) {
					resolve_dialog(dres);
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
					return;
				}
			}
		}

		if (active_popup_) {			auto res = active_popup_->handle_mouse(ev.mouse_x, ev.mouse_y);
			if (res.has_value()) {
				int id = res.value();
				active_popup_.reset();
				set_focus(focus_target::window, "popup_close");
				
				if (id != popup_menu::cancel_id) {
					editor_event action_ev;
					action_ev.type = static_cast<event_type>(id);
					global_queue_.push(action_ev);
				}
				
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
		}

		if (ev.mouse_y == 0 || top_menu_.is_open()) {
			if (top_menu_.handle_mouse(ev.mouse_x, ev.mouse_y, global_queue_)) {
				// Click was handled by the menu bar
				if (top_menu_.is_open()) {
					set_focus(focus_target::menu_bar, "mouse_click");
				} else {
					set_focus(focus_target::window, "menu_close");
				}
				editor_event redraw_ev;
				redraw_ev.type = event_type::redraw;
				global_queue_.push(redraw_ev);
				return;
			}
		}
	
		if (ev.mouse_y > 0) {
			// Find window under click (Reverse Z-order)
			std::vector<window*> sorted_windows;
			for (auto& w : windows_) {
				sorted_windows.push_back(w.get());
			}
	
			window *active_win = get_active_window();
			std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window* a, window* b) {
				int priority_a = (a == active_win) ? 9999 : a->get_display_priority();
				int priority_b = (b == active_win) ? 9999 : b->get_display_priority();
				
				if (priority_a != priority_b) {
					return priority_a > priority_b;
				}
				return a->get_last_active_timestamp() > b->get_last_active_timestamp();
			});
	
			for (auto it = sorted_windows.begin(); it != sorted_windows.end(); ++it) {
				window* w = *it;
				if (!w->is_visible()) continue;
	
				if (ev.mouse_x >= w->get_x() && ev.mouse_x < w->get_x() + w->get_width() &&
				    ev.mouse_y >= w->get_y() && ev.mouse_y < w->get_y() + w->get_height()) {
					
					// Found the topmost window under the click
					
					// 1. Check for Close Button [■]
					// The close button is drawn at (y_, x_ + 2) through (y_, x_ + 4)
					if (ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + 2 && ev.mouse_x <= w->get_x() + 4) {
						logger.log("Mouse clicked close button on window.");
						// Activate the window first so close_window acts on it
						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						editor_event close_ev;
						close_ev.type = event_type::close_window;
						global_queue_.push(close_ev);
						return;
					}

					// 1.25 Check for Popup Menu Button [≡]
					// Drawn at (y_, x_ + width_ - 10) through (y_, x_ + width_ - 8)
					if (ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + w->get_width() - 10 && ev.mouse_x <= w->get_x() + w->get_width() - 8) {
						logger.log("Mouse clicked popup menu button on window.");
						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						
						std::vector<popup_menu_item> items;
						items.push_back({static_cast<int>(event_type::save), "Save", 'S', false});
						items.push_back({static_cast<int>(event_type::git_add), "Git Add", 'G', false});
						items.push_back({static_cast<int>(event_type::compile_file), "Compile File", 'C', false});
						items.push_back({0, "", 0, true});
						items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
						
						active_popup_ = std::make_unique<popup_menu>(w->get_popup_button_x(), w->get_y() + 1, items);
						set_focus(focus_target::popup, "mouse_click");
						
						editor_event redraw_ev;
						redraw_ev.type = event_type::redraw;
						global_queue_.push(redraw_ev);
						return;
					}

					// 1.5 Check for Git Status Button
					// Drawn at x_ + 6
					int git_w = w->get_git_button_width();
					if (git_w > 0 && ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + 6 && ev.mouse_x < w->get_x() + 6 + git_w) {
						logger.log("Mouse clicked git status button.");
						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						editor_event git_ev;
						git_ev.type = event_type::git_add;
						global_queue_.push(git_ev);
						return;
					}

					// 1.75 Check for Modified Indicator
					auto doc = w->get_document();
					if (doc && doc->is_modified()) {
						std::string title = w->get_displayed_title();
						if (!title.empty() && title.back() == '*') {
							int title_x = w->get_x() + (w->get_width() - title.length()) / 2;
							int star_x = title_x + static_cast<int>(title.length()) - 1;
							
							if (ev.mouse_y == w->get_y() && ev.mouse_x == star_x) {
								logger.log("Mouse clicked modified indicator.");
								for (size_t i = 0; i < windows_.size(); ++i) {
									if (windows_[i].get() == w) {
										activate_window(i);
										break;
									}
								}
								editor_event save_ev;
								save_ev.type = event_type::save;
								global_queue_.push(save_ev);
								return;
							}
						}
					}

					// 2. Clicked inside the window content
					for (size_t i = 0; i < windows_.size(); ++i) {
						if (windows_[i].get() == w) {
							activate_window(i);
							set_focus(focus_target::window, "mouse_click");
							
							// Optional: Move cursor to clicked text
							if (ev.mouse_y >= w->get_y() + 1 && ev.mouse_y <= w->get_y() + w->get_height() - 2) {
								auto doc = w->get_document();
								if (doc) {
									// int click_line_offset = ev.mouse_y - w->get_y() - 1;
									// A simpler approach for now: just activate the window. Moving cursor accurately requires exposing the window's top_line_.
								}
							}
	
							editor_event redraw_ev;
							redraw_ev.type = event_type::redraw;
							global_queue_.push(redraw_ev);
							return;
						}
					}
				}
			}
		}
	} else if (ev.type == event_type::mouse_scroll_up || ev.type == event_type::mouse_scroll_down) {
		// Find window under mouse
		for (auto& w_ptr : windows_) {
			window* w = w_ptr.get();
			if (ev.mouse_x >= w->get_x() && ev.mouse_x < w->get_x() + w->get_width() &&
			    ev.mouse_y >= w->get_y() && ev.mouse_y < w->get_y() + w->get_height()) {
				
				auto doc = w->get_document();
				if (doc) {
					if (ev.type == event_type::mouse_scroll_up) {
						doc->move_cursor(0, -3); // Scroll up by 3 lines
					} else {
						doc->move_cursor(0, 3); // Scroll down by 3 lines
					}
					
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
				return;
			}
		}
	}

}
