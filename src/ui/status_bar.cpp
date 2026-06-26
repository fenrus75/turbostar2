#include "ui/status_bar.h"
#include <ncurses.h>
#include <chrono>
#include <ctime>
#include <cstring>
#include <cstdio>

namespace
{
void print_with_hotkeys(const std::string &str, int limit_x)
{
	bool highlight_next = false;
	for (char c : str) {
		int cur_x = getcurx(stdscr);
		if (cur_x >= limit_x) {
			break;
		}
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
} // namespace

void status_bar::draw(const std::string &mode_help, const std::string &hover_text, int cursor_x, int cursor_y, bool has_history) const
{
	(void)has_history;
	int max_y, max_x;
	getmaxyx(stdscr, max_y, max_x);

	move(max_y - 1, 0);
	attron(COLOR_PAIR(1));

	// Clear the status bar line
	for (int i = 0; i < max_x; ++i) {
		addch(' ');
	}

	// Get current local system time
	auto now = std::chrono::system_clock::now();
	auto time_t_now = std::chrono::system_clock::to_time_t(now);
	std::tm tm_now;
#ifdef _WIN32
	localtime_s(&tm_now, &time_t_now);
#else
	localtime_r(&time_t_now, &tm_now);
#endif
	char time_str[16];
	std::strftime(time_str, sizeof(time_str), "%H:%M", &tm_now);

	std::string clock_display = std::string(" ") + time_str + " ";
	int clock_len = static_cast<int>(clock_display.length());
	int limit_x = max_x - clock_len;

	move(max_y - 1, 0);

	if (!mode_help.empty()) {
		if (getcurx(stdscr) < limit_x) addstr(" ");
		print_with_hotkeys(mode_help, limit_x);
		if (getcurx(stdscr) < limit_x) addstr(" ");
	} else {
		// Cursor position
		if (cursor_x >= 0 && cursor_y >= 0) {
			char pos_str[32];
			std::snprintf(pos_str, sizeof(pos_str), " %d:%d ", cursor_y + 1, cursor_x + 1);
			if (getcurx(stdscr) + static_cast<int>(std::strlen(pos_str)) <= limit_x) {
				addstr(pos_str);
			}
		}

		// Default status bar content like "F1 Help"
		if (getcurx(stdscr) < limit_x) {
			print_with_hotkeys("  ^F^1 Help", limit_x);
		}
	}

	if (!hover_text.empty()) {
		// Print hover text truncated if needed
		if (getcurx(stdscr) + 3 <= limit_x) {
			addstr(" | ");
			print_with_hotkeys(hover_text, limit_x);
		}
	}

	// Draw the clock right-aligned
	if (max_x > clock_len) {
		mvaddstr(max_y - 1, max_x - clock_len, clock_display.c_str());
	}

	attroff(COLOR_PAIR(1));
}