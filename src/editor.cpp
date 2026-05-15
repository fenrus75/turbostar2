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
		int ch = getch();
		if (ch != ERR) {
			editor_event ev;
			if (ch == 27) { // ESC sequence
				nodelay(stdscr, TRUE);
				int next_ch = getch();
				if (next_ch == '[') { // Arrow keys start with ESC-[
					int arrow_ch = getch();
					if (arrow_ch != ERR) {
						int key = 0;
						switch(arrow_ch) {
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
				} else if (next_ch != ERR) {
					// Alt + key
					ev.type = event_type::key_press;
					ev.key_code = -next_ch;
					global_queue_.push(ev);
				} else {
					ev.type = event_type::key_press;
					ev.key_code = 27; // Bare ESC
					global_queue_.push(ev);
				}
				nodelay(stdscr, FALSE);
			} else {
				ev.type = event_type::key_press;
				ev.key_code = ch;
				global_queue_.push(ev);
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

void editor::dispatch(const editor_event& ev)
{
	auto& logger = event_logger::get_instance();
	if (ev.type == event_type::quit) {
		logger.log("Dispatching quit event.");
		is_running_ = false;
	} else if (ev.type == event_type::key_press) {
		logger.log("Dispatching key_press event: " + std::to_string(ev.key_code));
		
		if (ev.key_code < 0) { // Alt + key
			if (top_menu_.handle_alt_key(static_cast<char>(-ev.key_code), global_queue_)) {
				return;
			}
		}
		
		if (top_menu_.is_open()) {
			if (top_menu_.handle_key(ev.key_code, global_queue_)) {
				return;
			}
		}

		// Fallback to quit using Ctrl-C
logger.log("Dispatching key_press: " + std::to_string(ev.key_code));
		if (ev.key_code == 3) {
			editor_event quit_ev;
			quit_ev.type = event_type::quit;
			global_queue_.push(quit_ev);
			return;
		}

		// Route to active window
		for (auto& w : windows_) {
			if (w->is_active()) {
				if (ev.key_code == KEY_UP || ev.key_code == KEY_DOWN || 
				    ev.key_code == KEY_LEFT || ev.key_code == KEY_RIGHT) {
					w->get_queue().push(ev);
				} else {
					logger.log("Routing key_press to window: " + std::to_string(ev.key_code));
					editor_event win_ev;
					win_ev.type = event_type::key_press;
					win_ev.key_code = ev.key_code;
					w->get_queue().push(win_ev);
				}
				break;
			}
		}
	}
}

void editor::render()
{
	// Paint desktop background
	attron(COLOR_PAIR(3));
	for (int y = 1; y < LINES - 1; ++y) {
		move(y, 0);
		for (int x = 0; x < COLS; ++x) {
			addch(' ');
		}
	}
	attroff(COLOR_PAIR(3));

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

	bottom_status_.draw(debug_out, cur_x, cur_y);
	
	// Position hardware cursor
	for (const auto& w : windows_) {
		if (w->is_active()) {
			w->set_cursor_position();
			break;
		}
	}
	curs_set(1); // Ensure cursor is visible
	
	refresh();
}
