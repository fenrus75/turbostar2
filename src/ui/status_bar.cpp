#include "ui/status_bar.h"
#include <ncurses.h>

namespace {
	void print_with_hotkeys(const std::string& str) {
		bool highlight_next = false;
		for (char c : str) {
			if (c == '^' && !highlight_next) {
				highlight_next = true;
				continue;
			}
			if (highlight_next) {
				attron(COLOR_PAIR(2));
				addch(c);
				attroff(COLOR_PAIR(2));
				attron(COLOR_PAIR(1));
				highlight_next = false;
			} else {
				addch(c);
			}
		}
	}
}

void status_bar::draw(const std::string &mode_help, const std::string &hover_text, int cursor_x, int cursor_y) const
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);

	move(max_y - 1, 0);
	attron(COLOR_PAIR(1));

	// Clear the status bar line
	for (int i = 0; i < max_x; ++i) {
		addch(' ');
	}

	move(max_y - 1, 0);

	if (!mode_help.empty()) {
		addstr(" ");
		print_with_hotkeys(mode_help);
		addstr(" ");
	} else {
		// Cursor position
		if (cursor_x >= 0 && cursor_y >= 0) {
			printw(" %d:%d ", cursor_y + 1, cursor_x + 1);
		}

		// Default status bar content like "F1 Help"
		print_with_hotkeys("  ^F^1 Help");
	}

	if (!hover_text.empty()) {
		// Print hover text truncated if needed
		addstr(" | ");
		print_with_hotkeys(hover_text);
	}

	attroff(COLOR_PAIR(1));
}
