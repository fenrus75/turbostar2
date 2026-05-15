#include "status_bar.h"
#include <ncurses.h>

void status_bar::draw(const std::string& debug_message) const
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);

	move(max_y - 1, 0);
	attron(COLOR_PAIR(1));

	// Fill the bottom bar
	for (int i = 0; i < max_x; ++i) {
		addch(' ');
	}

	move(max_y - 1, 0);

	if (!debug_message.empty()) {
		printw(" %s ", debug_message.c_str());
	} else {
		// Default status bar content like "F1 Help"
		printw(" ");
		attron(COLOR_PAIR(2));
		printw("F1");
		attroff(COLOR_PAIR(2));
		attron(COLOR_PAIR(1));
		printw(" Help");
	}

	attroff(COLOR_PAIR(1));
}
