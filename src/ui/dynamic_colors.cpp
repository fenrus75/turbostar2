#include "ui/dynamic_colors.h"
#include <ncursesw/ncurses.h>
#include <map>
#include <cmath>
#include <algorithm>

namespace dynamic_colors
{

#define RGB_TO_CURSES(c) ((int)(((c) * 1000) / 255))

std::mutex dynamic_color_mutex;

static std::map<unsigned int, unsigned int> color2pair;
static std::map<unsigned int, unsigned int> rgb2color;
static int pairindex = 512;
static int colindex = 64;

int dynamic_get_color(int r, int g, int b)
{
	std::lock_guard<std::mutex> lock(dynamic_color_mutex);

	if (can_change_color()) {
		// Use dynamic palette modification (high color fidelity)
		int r_q = r / 32;
		int g_q = g / 32;
		int b_q = b / 32;

		int col = 64 * r_q + 8 * g_q + b_q;

		if (rgb2color.contains(col))
			return rgb2color[col];

		if (colindex < COLORS - 1) {
			init_extended_color(++colindex, RGB_TO_CURSES(r_q * 32), RGB_TO_CURSES(g_q * 32), RGB_TO_CURSES(b_q * 32));
			rgb2color[col] = colindex;
			return colindex;
		}
	}

	// Fallback to predefined 256-color cube / grayscale (maximum compatibility)
	int r_diff = std::abs(r - g);
	int g_diff = std::abs(g - b);
	int b_diff = std::abs(b - r);

	if (r_diff < 16 && g_diff < 16 && b_diff < 16) {
		int avg = (r + g + b) / 3;
		return 232 + (avg * 24) / 256;
	}

	int r_cube = (r * 5) / 255;
	int g_cube = (g * 5) / 255;
	int b_cube = (b * 5) / 255;
	return 16 + 36 * r_cube + 6 * g_cube + b_cube;
}

int dynamic_alloc_pair(int fg, int bg)
{
	std::lock_guard<std::mutex> lock(dynamic_color_mutex);

	unsigned int col = 512 * fg + bg;

	if (color2pair.contains(col))
		return color2pair[col];

	if (pairindex < COLOR_PAIRS - 1) {
		if (can_change_color()) {
			init_extended_pair(++pairindex, fg, bg);
		} else {
			init_pair(++pairindex, fg, bg);
		}
		color2pair[col] = pairindex;
		return pairindex;
	}
	return 1; // Default fallback pair
}

void reset_dynamic_colors()
{
	std::lock_guard<std::mutex> lock(dynamic_color_mutex);
	color2pair.clear();
	rgb2color.clear();
	pairindex = 512;
	colindex = 64;
}

} // namespace dynamic_colors
