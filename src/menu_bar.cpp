#include "menu_bar.h"
#include <ncurses.h>
#include <string>

void menu_bar::draw() const
{
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);
	(void)max_y; // Unused in menu_bar

	// Color pair 1: Black on White (Light Gray)
	// Color pair 2: Red on White (Light Gray)
	
	move(0, 0);
	attron(COLOR_PAIR(1));
	
	// Fill the top bar
	for (int i = 0; i < max_x; ++i) {
		addch(' ');
	}
	
	// Basic Turbo Pascal style menu
	// "File Edit Search Run Compile Debug Tools Options Window Help"
	const char* menus[] = {"File", "Edit", "Search", "Run", "Compile", "Debug", "Tools", "Options", "Window", "Help"};
	int col = 1;
	
	for (const auto& m : menus) {
		move(0, col);
		// First letter is red
		attron(COLOR_PAIR(2));
		addch(m[0]);
		attroff(COLOR_PAIR(2));
		
		// Rest is black
		attron(COLOR_PAIR(1));
		printw("%s", m + 1);
		
		col += 2 + std::string(m).length(); // Space between menus
	}
	
	attroff(COLOR_PAIR(1));
}
