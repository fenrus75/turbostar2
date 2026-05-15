#include "editor.h"
#include "event_logger.h"
#include <ncurses.h>

editor::editor(bool debug_mode, const std::string& debug_string, const std::string& filename)
	: debug_mode_(debug_mode), debug_string_(debug_string)
{
	// Create an initial document
	auto doc = std::make_shared<document>(filename);
	documents_.push_back(doc);

	// Create an initial window and attach the document
	// Full screen: x=0, y=1 (below menu), width=COLS, height=LINES-2 (above status bar)
	auto win = std::make_unique<window>(1, 0, 1, COLS, LINES - 2, filename);
	win->attach_document(doc);
	win->set_active(true);
	windows_.push_back(std::move(win));
}

void editor::run()
{
	render();

	// Set ncurses getch timeout to 50ms to allow background events to process
	timeout(50);

	while (is_running_) {
		wint_t wch;
		int res = get_wch(&wch);
		if (res != ERR) {
			editor_event ev;
			if (res == KEY_CODE_YES) {
				// Functional keys
				ev.type = event_type::key_press;
				ev.key_code = static_cast<int>(wch);
				global_queue_.push(ev);
			} else {
				// Character or ESC sequence
				if (wch == 27) { // ESC sequence
					nodelay(stdscr, TRUE);
					wint_t next_wch;
					int next_res = get_wch(&next_wch);
					if (next_res != ERR && next_res != KEY_CODE_YES && next_wch == '[') {
						wint_t arrow_wch;
						int arrow_res = get_wch(&arrow_wch);
						if (arrow_res != ERR && arrow_res != KEY_CODE_YES) {
							int key = 0;
							switch(arrow_wch) {
								case 'A': key = KEY_UP; break;
								case 'B': key = KEY_DOWN; break;
								case 'C': key = KEY_RIGHT; break;
								case 'D': key = KEY_LEFT; break;
							}
							if (key != 0) {
								ev.type = event_type::key_press;
								ev.key_code = key;
								global_queue_.push(ev);
							}
						}
					} else if (next_res != ERR) {
						// Alt + key
						ev.type = event_type::key_press;
						ev.key_code = -static_cast<int>(next_wch);
						global_queue_.push(ev);
					} else {
						ev.type = event_type::key_press;
						ev.key_code = 27; // Bare ESC
						global_queue_.push(ev);
					}
					nodelay(stdscr, FALSE);
				} else {
					// Printable character or UTF-8 sequence
					ev.type = event_type::key_press;
					ev.key_code = static_cast<int>(wch);
					
					// Convert wide char to UTF-8 string
					char buf[8];
					int len = wctomb(buf, wch);
					if (len > 0) {
						ev.utf8_char.assign(buf, len);
					}
					
					global_queue_.push(ev);
				}
			}
		}

		bool needs_render = false;
		while (auto ev = global_queue_.pop()) {
			dispatch(*ev);
			needs_render = true;
		}

		for (auto& w : windows_) {
			if (w->process_events()) {
				needs_render = true;
			}
		}

		if (needs_render) {
			render();
		}
	}
}

void editor::set_focus(focus_target target, const std::string& source)
{
	std::string target_name;
	switch(target) {
		case focus_target::menu_bar: target_name = "menu_bar"; break;
		case focus_target::window: target_name = "window"; break;
		case focus_target::dialog: target_name = "dialog"; break;
	}
	
	event_logger::get_instance().log("Focus change: " + source + " -> " + target_name);
	current_focus_ = target;
}

bool editor::handle_k_block_key(int key)
{
	auto& logger = event_logger::get_instance();
	logger.log("K-block handling key: " + std::to_string(key));
	
	char c = 0;
	if (key > 0 && key <= 26) {
		c = static_cast<char>(key + 'a' - 1);
	} else if (key >= 0 && key < 256) {
		c = std::tolower(static_cast<char>(key));
	}
	
	if (c == 0) return false;

	for (auto& w : windows_) {
		if (w->is_active()) {
			auto doc = documents_[0]; // Simplified for now, should find doc from window
			if (c == 'b') {
				logger.log("K-block: Set Selection Begin");
				doc->set_selection_start();
				return true;
			} else if (c == 'k') {
				logger.log("K-block: Set Selection End");
				doc->set_selection_end();
				return true;
			} else if (c == 'n') {
				logger.log("K-block: New File");
				doc->clear();
				return true;
			} else if (c == 'd') {
				logger.log("K-block: Save File");
				doc->save();
				return true;
			} else if (c == 'c') {
				logger.log("K-block: Copy Block");
				doc->copy_selection();
				return true;
			} else if (c == 'm') {
				logger.log("K-block: Move Block");
				doc->move_selection();
				return true;
			} else if (c == 'e') {
				logger.log("K-block: Edit (Load File)");
				active_dialog_ = std::make_unique<input_dialog>("Load File", "Enter filename to load:", doc->get_filename());
				active_dialog_mode_ = dialog_mode::load;
				set_focus(focus_target::dialog, "k_block");
				return true;
			} else if (c == 'w') {
				logger.log("K-block: Write (Save As)");
				active_dialog_ = std::make_unique<input_dialog>("Save File As", "Enter filename to save:", doc->get_filename());
				active_dialog_mode_ = dialog_mode::save;
				set_focus(focus_target::dialog, "k_block");
				return true;
			} else if (c == 'q') {
				logger.log("K-block: Quit (Abort)");
				editor_event quit_ev;
				quit_ev.type = event_type::quit;
				global_queue_.push(quit_ev);
				return true;
			} else if (c == 'x') {
				logger.log("K-block: Save & Exit");
				doc->save();
				editor_event quit_ev;
				quit_ev.type = event_type::quit;
				global_queue_.push(quit_ev);
				return true;
			} else if (c == 'u') {
				logger.log("K-block: Top of File");
				doc->move_to_top();
				return true;
			} else if (c == 'v') {
				logger.log("K-block: End of File");
				doc->move_to_bottom();
				return true;
			} else if (c == 'y') {
				logger.log("K-block: Delete Block");
				doc->delete_selection();
				return true;
			} else if (c == 'h') {
				logger.log("K-block: Clear Selection");
				doc->clear_selection();
				return true;
			}
		}
	}
	
	return false;
}

void editor::dispatch(const editor_event& ev)
{
	auto& logger = event_logger::get_instance();
	
	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::load) {
		logger.log("Dispatching load event.");
		auto doc = documents_[0];
		active_dialog_ = std::make_unique<input_dialog>("Load File", "Enter filename to load:", doc->get_filename());
		active_dialog_mode_ = dialog_mode::load;
		set_focus(focus_target::dialog, "menu_load");
		return;
	}

	if (ev.type == event_type::save) {
		logger.log("Dispatching save event.");
		auto doc = documents_[0];
		active_dialog_ = std::make_unique<input_dialog>("Save File As", "Enter filename to save:", doc->get_filename());
		active_dialog_mode_ = dialog_mode::save;
		set_focus(focus_target::dialog, "menu_save");
		return;
	}

	if (ev.type == event_type::new_doc) {
		logger.log("Dispatching new_doc event.");
		documents_[0]->clear();
		return;
	}

	if (ev.type == event_type::about) {
		logger.log("Dispatching about event.");
		std::vector<std::string> about_lines = {
			"TurboStar Editor",
			"Version 0.1.0",
			"",
			"A nostalgia inspired TUI editor",
			"",
			"Copyright (c) 2026",
			"Arjan van de Ven"
		};
		active_dialog_ = std::make_unique<message_dialog>("About TurboStar", about_lines);
		set_focus(focus_target::dialog, "menu_about");
		return;
	}

	if (ev.type == event_type::key_press) {
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));
		
		if (k_block_mode_) {
			if (handle_k_block_key(ev.key_code)) {
				k_block_mode_ = false;
				return;
			}
			k_block_mode_ = false;
			// Fall through if not handled, though typically K-block consumes or cancels
		}

		if (ev.key_code == 11) { // Ctrl-K
			logger.log("Entering K-block mode.");
			k_block_mode_ = true;
			return;
		}

		if (ev.key_code < 0) { // Alt + key
			if (top_menu_.handle_alt_key(static_cast<char>(-ev.key_code), global_queue_)) {
				set_focus(focus_target::menu_bar, "alt_key");
				return;
			}
		}

		// Route based on focus
		if (current_focus_ == focus_target::dialog && active_dialog_) {
			dialog_result res = active_dialog_->handle_key(ev.key_code);
			if (res == dialog_result::confirmed) {
				std::string path = active_dialog_->get_result();
				auto doc = documents_[0];
				if (active_dialog_mode_ == dialog_mode::load) {
					doc->load_from_file(path);
				} else if (active_dialog_mode_ == dialog_mode::save) {
					doc->save_to_file(path);
				}
				active_dialog_.reset();
				active_dialog_mode_ = dialog_mode::none;
				set_focus(focus_target::window, "dialog_close");
			} else if (res == dialog_result::cancelled) {
				active_dialog_.reset();
				active_dialog_mode_ = dialog_mode::none;
				set_focus(focus_target::window, "dialog_cancel");
			}
			return;
		}

		if (current_focus_ == focus_target::menu_bar) {
			if (top_menu_.handle_key(ev.key_code, global_queue_)) {
				if (!top_menu_.is_open()) {
					set_focus(focus_target::window, "menu_close");
				}
				return;
			}
		} else if (current_focus_ == focus_target::window) {
			// Fallback to quit using Ctrl-C
			if (ev.key_code == 3) {
				logger.log("Ctrl-C pressed (Ignored for direct quit, use ^KQ or ^KX)");
				return;
			}

			// Route to active window
			for (auto& w : windows_) {
				if (w->is_active()) {
					w->get_queue().push(ev);
					break;
				}
			}
		}
	}
}

void editor::render()
{
	curs_set(0); // Default to hidden

	// Paint desktop background with dithered pattern
	attron(COLOR_PAIR(9));
	for (int y = 1; y < LINES - 1; ++y) {
		move(y, 0);
		for (int x = 0; x < COLS; ++x) {
			addstr("▒");
		}
	}
	attroff(COLOR_PAIR(9));

	for (const auto& w : windows_) {
		w->draw();
	}

	top_menu_.draw();

	std::string debug_out;
	int cur_x = -1, cur_y = -1;

	if (debug_mode_) {
		auto& logger = event_logger::get_instance();
		auto msg = logger.get_latest_matching_message(debug_string_);
		if (msg) {
			debug_out = ">>" + *msg + "<<";
		}
	}

	for (const auto& w : windows_) {
		if (w->is_active()) {
			cur_x = w->get_cursor_x();
			cur_y = w->get_cursor_y();
			break;
		}
	}

	std::string status_help = debug_out;
	if (k_block_mode_) {
		status_help = "K-Block: B:Beg K:End Y:Del C:Copy M:Move U:Top V:End Q:Quit X:SaveExit";
	}

	bottom_status_.draw(status_help, cur_x, cur_y);

	if (active_dialog_) {
		active_dialog_->draw();
	}

	// Only show cursor if we are in window focus and NOT in a modal state
	if (current_focus_ == focus_target::window && !active_dialog_ && !k_block_mode_) {
		for (const auto& w : windows_) {
			if (w->is_active()) {
				w->set_cursor_position();
				curs_set(1); 
				break;
			}
		}
	}

	refresh();
}
