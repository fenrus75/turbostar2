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
#include "ui/agent_window.h"

namespace fs = std::filesystem;

void editor::dispatch_event_mouse(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::mouse_release) {
		if (active_dialog_) {
			active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			return;
		}
		if (current_drag_mode_ != drag_mode::none) {
			logger.log("Mouse drag released.");
			current_drag_mode_ = drag_mode::none;
			drag_window_ = nullptr;
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		} else {
			window *w = get_active_window();
			if (w) {
				w->get_window_queue().push(ev);
			}
		}
		return;
	}

	if (ev.type == event_type::mouse_drag) {
		if (active_dialog_) {
			active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			return;
		}
		if (current_drag_mode_ != drag_mode::none && drag_window_ != nullptr) {
			int dx = ev.mouse_x - drag_start_mouse_x_;
			int dy = ev.mouse_y - drag_start_mouse_y_;

			if (current_drag_mode_ == drag_mode::move) {
				if (drag_window_->is_maximized()) {
					logger.log("Auto-restoring maximized window on drag.");
					drag_window_->set_maximized(false);
					int rest_w = drag_window_->get_restore_width();
					int rest_h = drag_window_->get_restore_height();
					drag_start_win_w_ = rest_w;
					drag_start_win_h_ = rest_h;
					drag_start_win_x_ = std::clamp(drag_start_mouse_x_ - rest_w / 2, 0, COLS - rest_w);
					drag_start_win_y_ = std::clamp(drag_start_mouse_y_, 1, LINES - 1 - rest_h);
				}
				int new_x = drag_start_win_x_ + dx;
				int new_y = drag_start_win_y_ + dy;
				new_x = std::max(0, std::min(new_x, COLS - drag_start_win_w_));
				new_y = std::max(1, std::min(new_y, LINES - 1 - drag_start_win_h_));
				drag_window_->set_bounds(new_x, new_y, drag_start_win_w_, drag_start_win_h_);
			} else if (current_drag_mode_ == drag_mode::resize) {
				if (drag_window_->is_maximized()) {
					logger.log("Auto-restoring maximized window on resize.");
					drag_window_->set_maximized(false);
				}
				int new_w = drag_start_win_w_ + dx;
				int new_h = drag_start_win_h_ + dy;
				new_w = std::max(10, std::min(new_w, COLS - drag_start_win_x_));
				new_h = std::max(3, std::min(new_h, LINES - 1 - drag_start_win_y_));
				drag_window_->set_bounds(drag_start_win_x_, drag_start_win_y_, new_w, new_h);
			}

			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		} else {
			window *w = get_active_window();
			if (w) {
				w->get_window_queue().push(ev);
			}
		}
		return;
	}

	if (ev.type == event_type::mouse_click) {
		logger.log("Dispatching mouse click at X=" + std::to_string(ev.mouse_x) + " Y=" + std::to_string(ev.mouse_y));

		if (active_dialog_) {
			last_click_window_id_ = -1;
			last_click_on_title_bar_ = false;
			auto res = active_dialog_->handle_mouse(ev.mouse_x, ev.mouse_y);
			if (res.has_value()) {
				dialog_result dres = res.value();
				if (dres != dialog_result::pending) {
					resolve_dialog(dres);
				}
			}
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
			return;
		}

		if (active_popup_) {
			last_click_window_id_ = -1;
			last_click_on_title_bar_ = false;
			auto res = active_popup_->handle_mouse(ev.mouse_x, ev.mouse_y);
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
			last_click_window_id_ = -1;
			last_click_on_title_bar_ = false;
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
			std::vector<window *> sorted_windows;
			for (auto &w : windows_) {
				sorted_windows.push_back(w.get());
			}

			window *active_win = get_active_window();
			std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window *a, window *b) {
				int priority_a = (a == active_win) ? 9999 : a->get_display_priority();
				int priority_b = (b == active_win) ? 9999 : b->get_display_priority();

				if (priority_a != priority_b) {
					return priority_a > priority_b;
				}
				return a->get_last_active_timestamp() > b->get_last_active_timestamp();
			});

			for (auto it = sorted_windows.begin(); it != sorted_windows.end(); ++it) {
				window *w = *it;
				if (!w->is_visible())
					continue;

				if (ev.mouse_x >= w->get_x() && ev.mouse_x < w->get_x() + w->get_width() && ev.mouse_y >= w->get_y() &&
				    ev.mouse_y < w->get_y() + w->get_height()) {

					// Found the topmost window under the click


					// 1. Check for Close Button [■]
					// The close button is drawn at (y_, x_ + 2) through (y_, x_ + 4)
					if (ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + 2 && ev.mouse_x <= w->get_x() + 4) {
						logger.log("Mouse clicked close button on window.");
						last_click_window_id_ = -1;
						last_click_on_title_bar_ = false;

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
					if (ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + w->get_width() - 10 &&
					    ev.mouse_x <= w->get_x() + w->get_width() - 8) {
						logger.log("Mouse clicked popup menu button on window.");
						last_click_window_id_ = -1;
						last_click_on_title_bar_ = false;

						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}

						std::vector<popup_menu_item> items;
						std::string max_label = w->is_maximized() ? "Restore" : "Maximize";
						char max_hotkey = w->is_maximized() ? 'R' : 'M';

						if (auto aw = dynamic_cast<agent_window *>(w)) {
							items.push_back(
							    {static_cast<int>(event_type::agent_save_history), "Save History", 'S', false});
							if (aw->is_sidebar_expanded()) {
								items.push_back(
								    {static_cast<int>(event_type::agent_hide_sidebar), "Hide status pane", 'H', false});
							} else {
								items.push_back(
								    {static_cast<int>(event_type::agent_show_sidebar), "Show status pane", 'H', false});
							}
							items.push_back(
							    {static_cast<int>(event_type::maximize_window), max_label, max_hotkey, false});
							items.push_back({0, "", 0, true});
							items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
						} else {
							items.push_back({static_cast<int>(event_type::save), "Save", 'S', false});
							items.push_back({static_cast<int>(event_type::git_add), "Git Add", 'G', false});
							items.push_back(
							    {static_cast<int>(event_type::compile_file), "Compile File", 'C', false});
							items.push_back(
							    {static_cast<int>(event_type::maximize_window), max_label, max_hotkey, false});
							items.push_back({0, "", 0, true});
							items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
						}

						active_popup_ =
						    std::make_unique<popup_menu>(w->get_popup_button_x(), w->get_y() + 1, items);
						set_focus(focus_target::popup, "mouse_click");

						editor_event redraw_ev;
						redraw_ev.type = event_type::redraw;
						global_queue_.push(redraw_ev);
						return;
					}

					// 1.5 Check for Git Status Button
					// Drawn at x_ + 6
					int git_w = w->get_git_button_width();
					if (git_w > 0 && ev.mouse_y == w->get_y() && ev.mouse_x >= w->get_x() + 6 &&
					    ev.mouse_x < w->get_x() + 6 + git_w) {
						logger.log("Mouse clicked git status button.");
						last_click_window_id_ = -1;
						last_click_on_title_bar_ = false;

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
								last_click_window_id_ = -1;
								last_click_on_title_bar_ = false;

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

					// Check for bottom-right corner resize (drag-resize)
					if (ev.mouse_x == w->get_x() + w->get_width() - 1 &&
					    ev.mouse_y == w->get_y() + w->get_height() - 1) {
						last_click_window_id_ = -1;
						last_click_on_title_bar_ = false;

						logger.log("Mouse clicked bottom-right corner to resize.");
						current_drag_mode_ = drag_mode::resize;
						drag_window_ = w;
						drag_start_mouse_x_ = ev.mouse_x;
						drag_start_mouse_y_ = ev.mouse_y;
						drag_start_win_x_ = w->get_x();
						drag_start_win_y_ = w->get_y();
						drag_start_win_w_ = w->get_width();
						drag_start_win_h_ = w->get_height();

						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						editor_event redraw_ev;
						redraw_ev.type = event_type::redraw;
						global_queue_.push(redraw_ev);
						return;
					}

					// Check for title bar click (drag-move or double-click maximize)
					if (ev.mouse_y == w->get_y()) {
						auto now = std::chrono::steady_clock::now();
						if (last_click_window_id_ == w->get_id() && last_click_on_title_bar_ &&
						    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_click_time_).count() <=
							750) {
							logger.log("Double click detected on title bar. Maximizing/restoring window.");
							if (w->is_maximized()) {
								w->set_maximized(false);
								w->set_bounds(w->get_restore_x(), w->get_restore_y(),
									      w->get_restore_width(), w->get_restore_height());
							} else {
								w->save_restore_bounds();
								w->set_maximized(true);
								w->set_bounds(0, 1, COLS, LINES - 2);
							}

							// Clear double click state to prevent triple-clicks from triggering another toggle
							last_click_window_id_ = -1;
							last_click_on_title_bar_ = false;

							for (size_t i = 0; i < windows_.size(); ++i) {
								if (windows_[i].get() == w) {
									activate_window(i);
									break;
								}
							}
							editor_event redraw_ev;
							redraw_ev.type = event_type::redraw;
							global_queue_.push(redraw_ev);
							return;
						}

						// Record this click for potential future double-click
						last_click_window_id_ = w->get_id();
						last_click_on_title_bar_ = true;
						last_click_time_ = now;

						logger.log("Mouse clicked title bar to move.");
						current_drag_mode_ = drag_mode::move;
						drag_window_ = w;
						drag_start_mouse_x_ = ev.mouse_x;
						drag_start_mouse_y_ = ev.mouse_y;
						drag_start_win_x_ = w->get_x();
						drag_start_win_y_ = w->get_y();
						drag_start_win_w_ = w->get_width();
						drag_start_win_h_ = w->get_height();

						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						editor_event redraw_ev;
						redraw_ev.type = event_type::redraw;
						global_queue_.push(redraw_ev);
						return;
					}

					// 2. Clicked inside the window content
					last_click_window_id_ = -1;
					last_click_on_title_bar_ = false;
					for (size_t i = 0; i < windows_.size(); ++i) {
						if (windows_[i].get() == w) {
							activate_window(i);
							set_focus(focus_target::window, "mouse_click");

							// Forward mouse event to window local queue
							w->get_window_queue().push(ev);

							// Optional: Move cursor to clicked text
							if (ev.mouse_y >= w->get_y() + 1 &&
							    ev.mouse_y <= w->get_y() + w->get_height() - 2) {
								auto doc = w->get_document();
								if (doc) {
									// int click_line_offset = ev.mouse_y - w->get_y() - 1;
									// A simpler approach for now: just activate the window. Moving
									// cursor accurately requires exposing the window's top_line_.
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
		if (active_dialog_) {
			active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
			return;
		}
		// Find window under mouse (Z-order sorted, topmost first)
		std::vector<window *> sorted_windows;
		for (auto &w : windows_) {
			sorted_windows.push_back(w.get());
		}

		window *active_win = get_active_window();
		std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window *a, window *b) {
			int priority_a = (a == active_win) ? 9999 : a->get_display_priority();
			int priority_b = (b == active_win) ? 9999 : b->get_display_priority();

			if (priority_a != priority_b) {
				return priority_a > priority_b;
			}
			return a->get_last_active_timestamp() > b->get_last_active_timestamp();
		});

		for (window *w : sorted_windows) {
			if (!w->is_visible())
				continue;

			if (ev.mouse_x >= w->get_x() && ev.mouse_x < w->get_x() + w->get_width() && ev.mouse_y >= w->get_y() &&
			    ev.mouse_y < w->get_y() + w->get_height()) {

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
				} else {
					// Forward scroll event to window local queue
					w->get_window_queue().push(ev);
				}
				return;
			}
		}
	}
}
