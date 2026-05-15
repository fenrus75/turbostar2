#include "status_bar.h"
#include <ncurses.h>

void status_bar::draw(const std::string& debug_message, int cursor_x, int cursor_y) const
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

	if (!debug_message.empty()) {
		printw(" %s ", debug_message.c_str());
	} else {
		// Cursor position
		if (cursor_x >= 0 && cursor_y >= 0) {
			printw(" %d:%d ", cursor_y + 1, cursor_x + 1);
		}
		
		// Default status bar content like "F1 Help"
		printw("  F1 Help");
	}

	attroff(COLOR_PAIR(1));
}
