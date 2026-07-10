#include "ui/dynamic_colors.h"
#include <ncursesw/ncurses.h>
#include <map>
#include <cmath>
#include <algorithm>

namespace dynamic_colors
{

std::mutex dynamic_color_mutex;

static std::map<unsigned int, unsigned int> color2pair;
static int pairindex = 512;

int dynamic_get_color(int r, int g, int b)
{
	// Map RGB to standard 256-color palette (deterministic, no init_color needed)
	int r_diff = std::abs(r - g);
	int g_diff = std::abs(g - b);
	int b_diff = std::abs(b - r);

	if (r_diff < 16 && g_diff < 16 && b_diff < 16) {
		// Grayscale ramp: colors 232 to 255
		int avg = (r + g + b) / 3;
		return 232 + (avg * 24) / 256;
	}

	// 6x6x6 color cube: colors 16 to 231
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

	// Allocate a new pair index using standard init_pair
	if (pairindex < COLOR_PAIRS - 1) {
		init_pair(++pairindex, fg, bg);
		color2pair[col] = pairindex;
		return pairindex;
	}
	return 1; // Default fallback pair
}

void reset_dynamic_colors()
{
	std::lock_guard<std::mutex> lock(dynamic_color_mutex);
	color2pair.clear();
	pairindex = 512;
}

} // namespace dynamic_colors
