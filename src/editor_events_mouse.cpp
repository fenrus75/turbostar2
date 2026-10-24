#include <algorithm>
#include <chrono>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include <thread>
#include "build_error_manager.h"
#include "call_synthesizer.h"
#include "codereview_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "project_manager.h"
#include "ui/agent_window.h"

namespace fs = std::filesystem;

void editor::dispatch_event_mouse(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	// Modal Dialog mouse event interception:
	// If a modal dialog is active, we MUST route all mouse input events (clicks, releases, drags, scrolls)
	// directly to it and return immediately. Allowing any mouse event to fall through to the underlying
	// layers (like editor windows or menus) would cause them to gain focus or handle coordinates incorrectly,
	// breaking modality and leading to event leaks/corruptions.

	if (ev.type == event_type::mouse_release) {
		if (active_dialog_) {
			active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			return;
		}
		if (current_drag_mode_ != drag_mode::none) {
			logger.log("Mouse drag released.");
			current_drag_mode_ = drag_mode::none;
			drag_window_ = nullptr;
			global_queue_.push(event_type::redraw);
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

			global_queue_.push(event_type::redraw);
		} else {
			window *w = get_active_window();
			if (w) {
				w->get_window_queue().push(ev);
			}
		}
		return;
	}

	if (ev.type == event_type::mouse_right_click) {
		logger.log("Dispatching mouse right click at X=" + std::to_string(ev.mouse_x) + " Y=" + std::to_string(ev.mouse_y));

		if (active_dialog_) {
			return;
		}

		if (active_popup_) {
			active_popup_.reset();
			set_focus(focus_target::window, "popup_close");
			global_queue_.push(event_type::redraw);
			return;
		}

		if (ev.mouse_y <= 0 || ev.mouse_y >= LINES - 1) {
			return;
		}

		// Find topmost window under right click (Reverse Z-order)
		std::vector<window *> sorted_windows;
		sorted_windows.reserve(windows_.size());
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
			if (!w->is_visible()) {
				continue;
			}

			auto buf_pos = w->screen_to_buffer_pos(ev.mouse_x, ev.mouse_y);
			if (!buf_pos.has_value()) {
				continue;
			}

			// Activate window under click
			for (size_t i = 0; i < windows_.size(); ++i) {
				if (windows_[i].get() == w) {
					activate_window(i);
					break;
				}
			}
			set_focus(focus_target::window, "mouse_right_click");

			auto doc = w->get_document();
			if (!doc) {
				return;
			}

			int doc_x = buf_pos->first;
			int doc_y = buf_pos->second;
			doc->move_cursor(doc_x - doc->get_cursor_x(), doc_y - doc->get_cursor_y());

			auto ctx = turbostar::call_synthesizer::analyze_context_at(*doc, doc_y, doc_x);
			uint64_t current_generation = ++rmb_menu_generation_;

			std::vector<popup_menu_item> items;

			if (ctx.kind == turbostar::symbol_kind_context::function_call) {
				auto apply_call_mutation = [ctx](document &d, std::string_view call_text) {
					if (ctx.call_case == turbostar::call_site_case::full_call ||
					    (ctx.has_existing_parens && ctx.existing_args_end_col != -1)) {
						d.set_selection(ctx.target_line, ctx.existing_args_start_col,
								ctx.target_line, ctx.existing_args_end_col + 1);
						d.delete_selection();
						d.insert_text(std::string(call_text));
					} else if (ctx.call_case == turbostar::call_site_case::half_call ||
						   (ctx.has_existing_parens && ctx.existing_args_end_col == -1)) {
						auto l_obj = d.get_line(ctx.target_line);
						int len = l_obj ? l_obj->length_in_chars() : ctx.existing_args_start_col + 1;
						if (l_obj) {
							std::string lt = l_obj->get_text();
							size_t semi = lt.find(';', ctx.existing_args_start_col);
							if (semi != std::string::npos) {
								len = static_cast<int>(semi);
							}
						}
						d.set_selection(ctx.target_line, ctx.existing_args_start_col, ctx.target_line, len);
						d.delete_selection();
						d.insert_text(std::string(call_text));
					} else {
						// Naked identifier (Case 1)
						d.move_cursor(ctx.token_end_col - d.get_cursor_x(), ctx.target_line - d.get_cursor_y());
						d.insert_text(std::string(call_text));
					}
				};

				if (!ctx.type_synopsis.empty()) {
					std::string syn = ctx.type_synopsis;
					if (syn.length() > 44) {
						syn = syn.substr(0, 41) + "...";
					}
					items.push_back({0, syn, 0, false});
					items.push_back({0, "", 0, true});
				}

				// 1. Native Suggestions (Slots 1 and 2)
				for (size_t s_idx = 0; s_idx < std::min<size_t>(ctx.suggestions.size(), 2); ++s_idx) {
					const auto &s = ctx.suggestions[s_idx];
					char hotkey = static_cast<char>('1' + s_idx);
					std::string label = std::format("{}. {}", hotkey, s.call_text);
					popup_menu_item item{0, label, hotkey, false};
					item.on_select = [this, w_ptr = w, apply_call_mutation, call_text = s.call_text]() {
						auto d = w_ptr->get_document();
						if (!d) return;
						apply_call_mutation(*d, call_text);
						global_queue_.push(event_type::redraw);
					};
					items.push_back(item);
				}

				// 2. Slot 3: Reserved for AI Suggestion
				if (turbostar::call_synthesizer::has_ai_model_configured()) {
					int ai_item_index = static_cast<int>(items.size());
					popup_menu_item ai_item{0, "3. [ \u231b AI analyzing... ]", '3', false};
					ai_item.on_select = []() {};
					items.push_back(ai_item);

					// Capture context for background worker
					std::string enclosing_func;
					int start_l = std::max(0, doc_y - 25);
					int end_l = std::min(static_cast<int>(doc->get_line_count()), doc_y + 20);
					for (int l = start_l; l < end_l; ++l) {
						auto l_obj = doc->get_line(l);
						if (l_obj) {
							enclosing_func += l_obj->get_text() + "\n";
						}
					}
					auto locals = turbostar::call_synthesizer::extract_in_scope_variables(*doc, doc_y);
					std::string sig_str = !ctx.type_synopsis.empty() ? ctx.type_synopsis : ctx.token;
					std::string existing_args = ctx.existing_args;

					std::thread([this, current_generation, ai_item_index, w_ptr = w, apply_call_mutation,
						     enclosing_func = std::move(enclosing_func), sig_str = std::move(sig_str),
						     locals = std::move(locals), existing_args = std::move(existing_args)]() {
						auto ai_res = turbostar::call_synthesizer::query_ai_call_suggestion(
						    enclosing_func, sig_str, locals, existing_args);
						if (!ai_res.has_value() || ai_res->empty()) {
							return;
						}
						post_to_main_thread([this, current_generation, ai_item_index, w_ptr, apply_call_mutation, call_text = *ai_res]() {
							if (rmb_menu_generation_ != current_generation || !active_popup_) {
								return;
							}
							std::string label = "3. [AI] " + call_text;
							active_popup_->update_item(ai_item_index, label, '3', [this, w_ptr, apply_call_mutation, call_text]() {
								auto d = w_ptr->get_document();
								if (!d) return;
								apply_call_mutation(*d, call_text);
								global_queue_.push(event_type::redraw);
							});
							global_queue_.push(event_type::redraw);
						});
					}).detach();
				}

				if (!items.empty()) {
					items.push_back({0, "", 0, true}); // Separator
				}

				if (!ctx.declaration_location.empty()) {
					popup_menu_item def_item{0, "Go to Definition", 'D', false};
					def_item.on_select = [this, loc = ctx.declaration_location]() {
						editor_event oev;
						oev.type = event_type::open_file;
						oev.payload = loc;
						global_queue_.push(oev);
					};
					items.push_back(def_item);
				}

				popup_menu_item ref_item{0, "Find References", 'R', false};
				ref_item.on_select = [this, fn = doc->get_filename(), doc_y, doc_x]() {
					auto refs = project_manager::get_instance().lsp_query_references(fn, doc_y, doc_x);
					if (!refs.empty()) {
						editor_event oev;
						oev.type = event_type::open_file;
						oev.payload = std::format("{}:{}", refs[0].path, refs[0].range.start_y + 1);
						global_queue_.push(oev);
					}
				};
				items.push_back(ref_item);
			} else if (ctx.kind == turbostar::symbol_kind_context::type_definition ||
				   ctx.kind == turbostar::symbol_kind_context::variable) {
				if (!ctx.type_synopsis.empty()) {
					std::string syn = ctx.type_synopsis;
					if (syn.length() > 40) {
						syn = syn.substr(0, 37) + "...";
					}
					items.push_back({0, syn, 0, false});
					items.push_back({0, "", 0, true});
				}

				if (!ctx.declaration_location.empty()) {
					popup_menu_item def_item{0, "Go to Definition", 'D', false};
					def_item.on_select = [this, loc = ctx.declaration_location]() {
						editor_event oev;
						oev.type = event_type::open_file;
						oev.payload = loc;
						global_queue_.push(oev);
					};
					items.push_back(def_item);
				}

				popup_menu_item ref_item{0, "Find References", 'R', false};
				ref_item.on_select = [this, fn = doc->get_filename(), doc_y, doc_x]() {
					auto refs = project_manager::get_instance().lsp_query_references(fn, doc_y, doc_x);
					if (!refs.empty()) {
						editor_event oev;
						oev.type = event_type::open_file;
						oev.payload = std::format("{}:{}", refs[0].path, refs[0].range.start_y + 1);
						global_queue_.push(oev);
					}
				};
				items.push_back(ref_item);

				popup_menu_item fmt_item{static_cast<int>(event_type::format_doc), "Format Code", 'F', false};
				items.push_back(fmt_item);
			} else {
				if (doc->has_selection()) {
					popup_menu_item cut_item{0, "Cut Block (^KY)", 't', false};
					cut_item.on_select = [w_ptr = w]() {
						auto d = w_ptr->get_document();
						if (d) d->delete_selection();
					};
					items.push_back(cut_item);

					popup_menu_item copy_item{0, "Copy Block (^KC)", 'C', false};
					copy_item.on_select = [w_ptr = w]() {
						auto d = w_ptr->get_document();
						if (d) d->copy_selection();
					};
					items.push_back(copy_item);
				}

				popup_menu_item save_item{static_cast<int>(event_type::save), "Save", 'S', false};
				items.push_back(save_item);

				popup_menu_item fmt_item{static_cast<int>(event_type::format_doc), "Format Code", 'F', false};
				items.push_back(fmt_item);
			}

			if (items.empty()) {
				return;
			}

			int popup_x = std::clamp(ev.mouse_x + 1, 1, std::max(1, COLS - 35));
			int popup_y = std::clamp(ev.mouse_y + 1, 1, std::max(1, LINES - static_cast<int>(items.size()) - 3));
			active_popup_ = std::make_unique<popup_menu>(popup_x, popup_y, items);
			set_focus(focus_target::popup, "mouse_right_click");
			global_queue_.push(event_type::redraw);
			return;
		}
		return;
	}

	if (ev.type == event_type::mouse_click) {
		logger.log("Dispatching mouse click at X=" + std::to_string(ev.mouse_x) + " Y=" + std::to_string(ev.mouse_y));

		if (ev.mouse_y == LINES - 1) {
			logger.log("Mouse clicked status bar.");
			handle_status_bar_click(ev.mouse_x);
			return;
		}

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
			global_queue_.push(event_type::redraw);
			// CRITICAL: Return immediately. Clicks outside the dialog must NOT fall through to
			// underlying window components, as that would shift window focus and hijack keyboard input.
			return;
		}

		if (active_popup_) {
			last_click_window_id_ = -1;
			last_click_on_title_bar_ = false;
			auto res = active_popup_->handle_mouse(ev.mouse_x, ev.mouse_y);
			if (res.has_value()) {
				auto cb = active_popup_->get_selected_callback();
				int id = res.value();
				active_popup_.reset();
				set_focus(focus_target::window, "popup_close");

				if (cb) {
					cb();
				} else if (id != popup_menu::cancel_id) {
					global_queue_.push(static_cast<event_type>(id));
				}

				global_queue_.push(event_type::redraw);
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
				global_queue_.push(event_type::redraw);
				return;
			}
		}

		if (ev.mouse_y > 0) {
			// Find window under click (Reverse Z-order)
			std::vector<window *> sorted_windows;
			sorted_windows.reserve(windows_.size());
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
						global_queue_.push(event_type::close_window);
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
								items.push_back({static_cast<int>(event_type::agent_hide_sidebar),
										 "Hide status pane", 'H', false});
							} else {
								items.push_back({static_cast<int>(event_type::agent_show_sidebar),
										 "Show status pane", 'H', false});
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
							items.push_back({static_cast<int>(event_type::toggle_gutter_numbers),
									 "Toggle Line Numbers", 'N', false});
							items.push_back(
							    {static_cast<int>(event_type::maximize_window), max_label, max_hotkey, false});
							items.push_back({0, "", 0, true});
							items.push_back({static_cast<int>(event_type::close_window), "Close", 'l', false});
						}

						active_popup_ =
						    std::make_unique<popup_menu>(w->get_popup_button_x(), w->get_y() + 1, items);
						set_focus(focus_target::popup, "mouse_click");

						global_queue_.push(event_type::redraw);
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
						global_queue_.push(event_type::git_add);
						return;
					}

					// 1.6 Check for Gutter Mode Button
					int gutter_x = w->get_gutter_button_x();
					int gutter_w = w->get_gutter_button_width();
					if (gutter_w > 0 && ev.mouse_y == w->get_y() && ev.mouse_x >= gutter_x &&
					    ev.mouse_x < gutter_x + gutter_w) {
						logger.log("Mouse clicked gutter mode button.");
						last_click_window_id_ = -1;
						last_click_on_title_bar_ = false;

						for (size_t i = 0; i < windows_.size(); ++i) {
							if (windows_[i].get() == w) {
								activate_window(i);
								break;
							}
						}
						global_queue_.push(event_type::toggle_gutter_numbers);
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
								global_queue_.push(event_type::save);
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
						global_queue_.push(event_type::redraw);
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
							global_queue_.push(event_type::redraw);
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
						global_queue_.push(event_type::redraw);
						return;
					}

					// 2. Clicked inside the window content
					last_click_window_id_ = -1;
					last_click_on_title_bar_ = false;
					for (size_t i = 0; i < windows_.size(); ++i) {
						if (windows_[i].get() == w) {
							activate_window(i);
							set_focus(focus_target::window, "mouse_click");

							auto now = std::chrono::steady_clock::now();
							editor_event target_ev = ev;

							if (last_content_click_window_id_ == w->get_id() &&
							    last_content_click_x_ == ev.mouse_x && last_content_click_y_ == ev.mouse_y &&
							    std::chrono::duration_cast<std::chrono::milliseconds>(now -
														  last_content_click_time_)
								    .count() <= 500) {
								content_click_count_++;
								if (content_click_count_ == 2) {
									target_ev.type = event_type::mouse_double_click;
									logger.log("Software double click detected in content area.");
								} else if (content_click_count_ == 3) {
									target_ev.type = event_type::mouse_triple_click;
									logger.log("Software triple click detected in content area.");
								} else {
									content_click_count_ = 1;
								}
							} else {
								content_click_count_ = 1;
							}

							last_content_click_window_id_ = w->get_id();
							last_content_click_x_ = ev.mouse_x;
							last_content_click_y_ = ev.mouse_y;
							last_content_click_time_ = now;

							// Forward mouse event to window local queue
							w->get_window_queue().push(target_ev);

							global_queue_.push(event_type::redraw);
							return;
						}
					}
				}
			}
		}
	} else if (ev.type == event_type::mouse_scroll_up || ev.type == event_type::mouse_scroll_down) {
		if (active_dialog_) {
			active_dialog_->handle_event(ev, active_dialog_->x(), active_dialog_->y());
			global_queue_.push(event_type::redraw);
			// Swallowed by active dialog
			return;
		}
		// Find window under mouse (Z-order sorted, topmost first)
		std::vector<window *> sorted_windows;
		sorted_windows.reserve(windows_.size());
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

					global_queue_.push(event_type::redraw);
				} else {
					// Forward scroll event to window local queue
					w->get_window_queue().push(ev);
				}
				return;
			}
		}
	}
}
