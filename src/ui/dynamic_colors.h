#pragma once

#include <mutex>

namespace dynamic_colors
{

// Mutex protecting global color cache mappings and index counters.
// 
// 1. What it protects:
//    - The `color2pair` map mapping combined foreground/background colors to pair indices.
//    - The `rgb2color` map mapping combined RGB values to ncurses color indices.
//    - The `pairindex` and `colindex` allocation counters.
// 2. Locking rules:
//    - Acquire this lock inside dynamic_get_color, dynamic_alloc_pair, and reset_dynamic_colors before accessing or modifying caches.
//    - Only lock briefly. Do not make external calls or hold the lock across rendering frames.
extern std::mutex dynamic_color_mutex;

int dynamic_get_color(int r, int g, int b);
int dynamic_alloc_pair(int fg, int bg);
void reset_dynamic_colors();

} // namespace dynamic_colors
