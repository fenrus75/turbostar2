#include "editor.h"
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "file_dialog.h"
#include "find_dialog.h"
#include "history_manager.h"

editor::editor(bool debug_mode, const std::string &debug_string, const std::string &filename, bool exit_immediately)
    : exit_immediately_(exit_immediately), debug_mode_(debug_mode), debug_string_(debug_string)
{
	history_manager::get_instance().load();
	new_window(filename);
}

void editor::new_window(const std::string &filename)
{
	// Create document
	auto doc = std::make_shared<document>(global_queue_, filename);
	documents_.push_back(doc);

	// Create window
	auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, filename);
	win->attach_document(doc);

	windows_.push_back(std::move(win));
	activate_window(windows_.size() - 1);
}

void editor::activate_window(size_t index)
{
	if (index >= windows_.size())
		return;

	for (size_t i = 0; i < windows_.size(); ++i) {
		windows_[i]->set_active(i == index);
	}
	update_window_menu();
}

void editor::update_window_menu()
{
	std::vector<menu_item> items;
	for (size_t i = 0; i < windows_.size(); ++i) {
		std::string filename = windows_[i]->get_title();
		if (filename.empty())
			filename = "noname.txt";

		auto doc = windows_[i]->get_document();
		if (doc && doc->is_modified()) {
			filename += "*";
		}

		std::string name = std::to_string((i + 1) % 10) + " " + filename;

		std::string shortcut = "";
		char hotkey = 0;
		if (i < 10) {
			int num = static_cast<int>((i + 1) % 10);
			shortcut = "Alt-" + std::to_string(num);
			hotkey = static_cast<char>('0' + num);
		}

		items.push_back(menu_item(name, event_type::select_window, static_cast<int>(i), hotkey, shortcut, false));
	}
	event_logger::get_instance().log("update_window_menu: " + std::to_string(items.size()) + " items");
	top_menu_.set_category_items("Window", items);
}

std::shared_ptr<document> editor::get_active_doc() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w->get_document();
		}
	}
	if (!documents_.empty()) {
		return documents_[0];
	}
	return nullptr;
}

window *editor::get_active_window() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w.get();
		}
	}
	return nullptr;
}

std::string editor::get_search_autocomplete() const
{
	if (search_input_buffer_.empty())
		return "";

	const auto &history = history_manager::get_instance().get_searches();
	for (const auto &item : history) {
		if (item.length() >= search_input_buffer_.length() &&
		    item.substr(0, search_input_buffer_.length()) == search_input_buffer_) {
			return item;
		}
	}
	return "";
}

void editor::run()
{
	render();

	// Set ncurses getch timeout to 50ms to allow background events to
	// process
	timeout(50);
	auto start_time = std::chrono::steady_clock::now();

	while (is_running_) {
		if (exit_immediately_) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 1000) {
				is_running_ = false;
			}
		}

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
							switch (arrow_wch) {
								case 'A':
									key = KEY_UP;
									break;
								case 'B':
									key = KEY_DOWN;
									break;
								case 'C':
									key = KEY_RIGHT;
									break;
								case 'D':
									key = KEY_LEFT;
									break;
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

		for (auto &w : windows_) {
			if (w->process_events()) {
				needs_render = true;
			}
		}

		if (needs_render) {
			render();
		}
	}

	// Save history on exit
	history_manager::get_instance().save();
}

bool editor::handle_q_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	int c = std::tolower(key);

	if (c == 'f') {
		logger.log("Q-block: Find Text");
		editor_event ev;
		ev.type = event_type::find;
		global_queue_.push(ev);
		return true;
	} else if (c == 'a') {
		logger.log("Q-block: Replace Text");
		editor_event ev;
		ev.type = event_type::replace;
		global_queue_.push(ev);
		return true;
	}
	return false;
}

void editor::set_focus(focus_target target, const std::string &source)
{
	std::string target_name;
	switch (target) {
		case focus_target::menu_bar:
			target_name = "menu_bar";
			break;
		case focus_target::window:
			target_name = "window";
			break;
		case focus_target::dialog:
			target_name = "dialog";
			break;
	}

	event_logger::get_instance().log("Focus change: " + source + " -> " + target_name);
	current_focus_ = target;

	if (target == focus_target::menu_bar) {
		update_window_menu();
	}
}

bool editor::handle_k_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	logger.log("K-block handling key: " + std::to_string(key));

	char c = 0;
	if (key > 0 && key <= 26) {
		c = static_cast<char>(key + 'a' - 1);
	} else if (key >= 0 && key < 256) {
		c = std::tolower(static_cast<char>(key));
	}

	if (c == 0)
		return false;

	// Find active window/doc
	std::shared_ptr<document> active_doc = get_active_doc();

	if (!active_doc)
		return false;

	if (c == 'b') {
		logger.log("K-block: Set Selection Begin");
		active_doc->set_selection_start();
		return true;
	} else if (c == 'k') {
		logger.log("K-block: Set Selection End");
		active_doc->set_selection_end();
		return true;
	} else if (c == 'h') {
		logger.log("K-block: Clear Selection");
		active_doc->clear_selection();
		return true;
	} else if (c == 'y') {
		logger.log("K-block: Delete Block");
		active_doc->delete_selection();
		return true;
	} else if (c == 'c') {
		logger.log("K-block: Copy Block");
		active_doc->copy_selection();
		return true;
	} else if (c == 'm') {
		logger.log("K-block: Move Block");
		active_doc->move_selection();
		return true;
	} else if (c == 'n') {
		logger.log("K-block: New Window");
		new_window("");
		return true;
	} else if (c == 'e') {
		logger.log("K-block: Edit (Open File)");
		editor_event ev;
		ev.type = event_type::load;
		global_queue_.push(ev);
		return true;
	} else if (c == 'd' || c == 's') {
		logger.log("K-block: Save File");
		editor_event ev;
		ev.type = event_type::save;
		global_queue_.push(ev);
		return true;
	} else if (c == 'w') {
		logger.log("K-block: Write (Save As)");
		editor_event ev;
		ev.type = event_type::save_as;
		global_queue_.push(ev);
		return true;
	} else if (c == 'q') {
		logger.log("K-block: Quit (Abort)");
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'x') {
		logger.log("K-block: Save & Exit");
		active_doc->save();
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'f') {
		logger.log("K-block: Find (Status Bar Prompt)");
		is_searching_prompt_ = true;
		search_input_buffer_ = "";
		return true;
	} else if (c == 'l') {
		logger.log("K-block: Go to Line (Status Bar Prompt)");
		is_going_to_line_prompt_ = true;
		line_input_buffer_ = "";
		return true;
	} else if (c == 'u') {
		logger.log("K-block: Top of File");
		active_doc->move_to_top();
		return true;
	} else if (c == 'v') {
		logger.log("K-block: End of File");
		active_doc->move_to_bottom();
		return true;
	}

	return false;
}

void editor::dispatch(const editor_event &ev)
{
	auto &logger = event_logger::get_instance();

	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_running_ = false;
		return;
	}

	if (ev.type == event_type::redraw) {
		logger.log("Dispatching redraw event.");
		return;
	}

	if (ev.type == event_type::load) {
		logger.log("Dispatching load event.");
		active_dialog_ = std::make_unique<file_dialog>("Open File", file_dialog_mode::open, true, ".");
		active_dialog_mode_ = dialog_mode::load;
		set_focus(focus_target::dialog, "menu_load");
		return;
	}

	if (ev.type == event_type::save) {
		logger.log("Dispatching save event (Smart Save).");
		std::shared_ptr<document> active_doc = get_active_doc();

		if (active_doc && active_doc->has_nondefault_filename()) {
			active_doc->save();
			history_manager::get_instance().add_file(active_doc->get_filename());
			// Update window title and menu to clear dirty flag
			for (auto &w : windows_) {
				if (w->get_document() == active_doc) {
					w->set_title(active_doc->get_filename());
					break;
				}
			}
			update_window_menu();
			return;
		}

		// Fallback to Save As logic
		editor_event save_as_ev;
		save_as_ev.type = event_type::save_as;
		dispatch(save_as_ev);
		return;
	}

	if (ev.type == event_type::save_as) {
		logger.log("Dispatching save_as event.");
		std::shared_ptr<document> active_doc = get_active_doc();

		std::string filename_arg;
		if (active_doc) {
			filename_arg = active_doc->get_filename();
		} else {
			filename_arg = ".";
		}
		active_dialog_ = std::make_unique<file_dialog>("Save File As", file_dialog_mode::save, false, filename_arg);
		active_dialog_mode_ = dialog_mode::save;
		set_focus(focus_target::dialog, "menu_save");
		return;
	}

	if (ev.type == event_type::new_doc) {
		logger.log("Dispatching new_doc event.");
		new_window("");
		return;
	}

	if (ev.type == event_type::select_window) {
		logger.log("Selecting window: " + std::to_string(ev.key_code));
		activate_window(static_cast<size_t>(ev.key_code));
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

	if (ev.type == event_type::find) {
		logger.log("Dispatching find event (advanced dialog).");
		active_dialog_ = std::make_unique<find_dialog>("Find", current_search_, false);
		active_dialog_mode_ = dialog_mode::search;
		set_focus(focus_target::dialog, "menu_find");
		return;
	}

	if (ev.type == event_type::replace) {
		logger.log("Dispatching replace event (advanced dialog).");
		active_dialog_ = std::make_unique<find_dialog>("Replace", current_search_, true);
		active_dialog_mode_ = dialog_mode::replace;
		set_focus(focus_target::dialog, "menu_replace");
		return;
	}

	if (ev.type == event_type::key_press) {
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));

		// 1. Modal Dialogs have highest priority
		if (current_focus_ == focus_target::dialog && active_dialog_) {
			dialog_result res = active_dialog_->handle_key(ev.key_code);
			if (res == dialog_result::confirmed) {
				auto doc = get_active_doc();
				if (!doc)
					doc = documents_[0]; // Default fallback

				if (active_dialog_mode_ == dialog_mode::load) {
					std::string result_path = active_dialog_->get_result();
					new_window(result_path);
					history_manager::get_instance().add_file(result_path);
				} else if (active_dialog_mode_ == dialog_mode::save) {
					std::string result_path = active_dialog_->get_result();
					doc->save_to_file(result_path);
					history_manager::get_instance().add_file(result_path);
					// Update window title
					for (auto &w : windows_) {
						if (w->get_document() == doc) {
							w->set_title(doc->get_filename());
							break;
						}
					}
					update_window_menu();
				} else if (active_dialog_mode_ == dialog_mode::search || active_dialog_mode_ == dialog_mode::replace) {
					auto f_dialog = dynamic_cast<find_dialog *>(active_dialog_.get());
					if (f_dialog) {
						current_search_ = f_dialog->get_search_params();
						history_manager::get_instance().add_search(current_search_.query);
						if (doc->find_next(current_search_)) {
							editor_event redraw_ev;
							redraw_ev.type = event_type::redraw;
							global_queue_.push(redraw_ev);
						}
					}
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

		// 2. Status Bar Search Prompt
		if (is_searching_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_searching_prompt_ = false;
				return;
			}
			std::string suggestion = get_search_autocomplete();
			
			if (ev.key_code == 9) { // TAB
				if (!suggestion.empty()) {
					search_input_buffer_ = suggestion;
				}
				return;
			}

			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				if (!suggestion.empty() && search_input_buffer_ != suggestion) {
					search_input_buffer_ = suggestion;
					return;
				}
				
				current_search_.query = search_input_buffer_;
				
				is_searching_prompt_ = false;
				is_search_options_prompt_ = true;
				search_options_buffer_ = "";
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!search_input_buffer_.empty())
					search_input_buffer_.pop_back();
				return;
			}
			if (!ev.utf8_char.empty() && ev.key_code >= 32) {
				search_input_buffer_ += ev.utf8_char;
				return;
			}
			return; // Consume all keys in prompt mode
		}

		// 2.5 Status Bar Search Options Prompt
		if (is_search_options_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_search_options_prompt_ = false;
				return;
			}
			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				// Parse options
				current_search_.backward = false;
				current_search_.selected_text_only = false;
				current_search_.ignore_case = false;
				current_search_.from_cursor = true;
				
				bool is_replace = false;
				for (char c : search_options_buffer_) {
					char lc = std::tolower(c);
					if (lc == 'b') current_search_.backward = true;
					if (lc == 'k') current_search_.selected_text_only = true;
					if (lc == 'i') current_search_.ignore_case = true;
					if (lc == 'r') is_replace = true;
				}
				
				history_manager::get_instance().add_search(current_search_.query);

				if (is_replace) {
					is_search_options_prompt_ = false;
					active_dialog_ = std::make_unique<find_dialog>("Replace", current_search_, true);
					active_dialog_mode_ = dialog_mode::replace;
					set_focus(focus_target::dialog, "menu_replace");
					return;
				}

				std::shared_ptr<document> active_doc = get_active_doc();

				if (active_doc && active_doc->find_next(current_search_)) {
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
				is_search_options_prompt_ = false;
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!search_options_buffer_.empty())
					search_options_buffer_.pop_back();
				return;
			}
			if (!ev.utf8_char.empty() && ev.key_code >= 32) {
				char c = std::tolower(ev.utf8_char[0]);
				if (c == 'i' || c == 'r' || c == 'b' || c == 'k') {
					if (search_options_buffer_.find(c) == std::string::npos && search_options_buffer_.find(std::toupper(c)) == std::string::npos) {
						search_options_buffer_ += static_cast<char>(std::toupper(c));
					}
				}
				return;
			}
			return; // Consume all keys in prompt mode
		}

		// 3. Status Bar Go to Line Prompt
		if (is_going_to_line_prompt_) {
			if (ev.key_code == 27) { // ESC
				is_going_to_line_prompt_ = false;
				return;
			}
			if (ev.key_code == 13 || ev.key_code == 10 || ev.key_code == KEY_ENTER) {
				try {
					if (!line_input_buffer_.empty()) {
						int line_num = std::stoi(line_input_buffer_);
						std::shared_ptr<document> active_doc = get_active_doc();
						if (active_doc) {
							// Move to line (convert 1-based to 0-based)
							active_doc->move_cursor(0, (line_num - 1) - active_doc->get_cursor_y());
						}
					}
				} catch (...) {
					// Ignore invalid input
				}
				is_going_to_line_prompt_ = false;
				return;
			}
			if (ev.key_code == KEY_BACKSPACE || ev.key_code == 127 || ev.key_code == 8) {
				if (!line_input_buffer_.empty())
					line_input_buffer_.pop_back();
				return;
			}
			if (ev.key_code >= '0' && ev.key_code <= '9') {
				line_input_buffer_ += static_cast<char>(ev.key_code);
				return;
			}
			return; // Consume all keys in prompt mode
		}

		if (ev.key_code == 12) { // Ctrl-L
			logger.log("Repeating last search.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				logger.log("Active doc found for ^L. query=" + current_search_.query +
					   " backward=" + std::to_string(current_search_.backward));
				bool found = active_doc->find_next(current_search_, true);
				logger.log("find_next returned: " + std::to_string(found));
				if (found) {
					editor_event redraw_ev;
					redraw_ev.type = event_type::redraw;
					global_queue_.push(redraw_ev);
				}
			}
			return;
		}

		if (k_block_mode_) {
			if (handle_k_block_key(ev.key_code)) {
				k_block_mode_ = false;
				return;
			}
			k_block_mode_ = false;
		}

		if (q_block_mode_) {
			if (handle_q_block_key(ev.key_code)) {
				q_block_mode_ = false;
				return;
			}
			q_block_mode_ = false;
		}

		if (ev.key_code == 11) { // Ctrl-K
			logger.log("Entering K-block mode.");
			k_block_mode_ = true;
			return;
		}

		if (ev.key_code == 17) { // Ctrl-Q
			logger.log("Entering Q-block mode.");
			q_block_mode_ = true;
			return;
		}

		if (ev.key_code == 31) { // Ctrl-_ (Undo)
			logger.log("Undo requested.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				active_doc->undo();
			}
			return;
		}

		if (ev.key_code == 30) { // Ctrl-^ (Redo)
			logger.log("Redo requested.");
			std::shared_ptr<document> active_doc = get_active_doc();
			if (active_doc) {
				active_doc->redo();
			}
			return;
		}

		if (ev.key_code < 0) { // Alt + key
			char c = static_cast<char>(-ev.key_code);
			if (c >= '0' && c <= '9') {
				int target_idx;
				if (c == '0') {
					target_idx = 9;
				} else {
					target_idx = (c - '1');
				}
				activate_window(static_cast<size_t>(target_idx));
				return;
			}
			if (top_menu_.handle_alt_key(c, global_queue_)) {
				set_focus(focus_target::menu_bar, "alt_key");
				return;
			}
		}

		// Route based on focus (Windows/Menu Bar)
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
				logger.log("Ctrl-C pressed (Ignored for direct "
					   "quit, use ^KQ or ^KX)");
				return;
			}

			// Route to active window
			window *active_win = get_active_window();
			if (active_win) {
				active_win->get_queue().push(ev);
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

	// 2. Windows (Foreground Only)
	window *active_win = get_active_window();
	if (active_win) {
		active_win->draw();
	}

	top_menu_.draw();

	std::string debug_out;
	int cur_x = -1, cur_y = -1;

	if (debug_mode_) {
		auto &logger = event_logger::get_instance();
		auto msg = logger.get_latest_matching_message(debug_string_);
		if (msg) {
			debug_out = ">>" + *msg + "<<";
		}
	}

	if (active_win) {
		cur_x = active_win->get_cursor_x();
		cur_y = active_win->get_cursor_y();
	}

	std::string status_help = debug_out;
	if (k_block_mode_) {
		status_help = "K-Block: B:Beg K:End Y:Del C:Copy M:Move U:Top "
			      "V:End Q:Quit X:SaveExit F:Find";
	} else if (q_block_mode_) {
		status_help = "Q-Block: F:Find A:Replace";
	} else if (is_searching_prompt_) {
		std::string suggestion = get_search_autocomplete();
		if (!suggestion.empty() && suggestion != search_input_buffer_) {
			status_help = "Search for: " + search_input_buffer_ + "[" + suggestion.substr(search_input_buffer_.length()) + "]";
		} else {
			status_help = "Search for: " + search_input_buffer_ + "_";
		}
	} else if (is_search_options_prompt_) {
		status_help = "Options (I R B K): " + search_options_buffer_ + "_";
	} else if (is_going_to_line_prompt_) {
		status_help = "Go to line: " + line_input_buffer_ + "_";
	}

	bottom_status_.draw(status_help, cur_x, cur_y);

	if (active_dialog_) {
		active_dialog_->draw();
	}

	// Only show cursor if we are in window focus and NOT in a modal state
	if (current_focus_ == focus_target::window && !active_dialog_ && !k_block_mode_) {
		if (active_win) {
			active_win->set_cursor_position();
			curs_set(1);
		}
	}

	refresh();
}
