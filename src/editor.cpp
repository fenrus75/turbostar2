#include "editor.h"
#include "event_logger.h"
#include <ncurses.h>

editor::editor(bool debug_mode, const std::string& debug_string)
	: debug_mode_(debug_mode), debug_string_(debug_string)
{
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
			if (ch == 'q') { // Temporary hardcoded quit key for the skeleton
				ev.type = event_type::quit;
			} else {
				ev.type = event_type::key_press;
				ev.key_code = ch;
			}
			global_queue_.push(ev);
		}

		bool needs_render = false;
		while (auto ev = global_queue_.pop()) {
			dispatch(*ev);
			needs_render = true;
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

	top_menu_.draw();

	std::string debug_out;
	if (debug_mode_) {
		auto& logger = event_logger::get_instance();
		auto msg = logger.get_latest_matching_message(debug_string_);
		if (msg) {
			debug_out = ">>" + *msg + "<<";
		}
	}

	bottom_status_.draw(debug_out);
	refresh();
}
