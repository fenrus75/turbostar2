#include "ui/components/ui_durmovie.h"
#include <algorithm>
#include <map>
#include <ncurses.h>
#include <nlohmann/json.hpp>
#include <utility>
#include "event_logger.h"
#include "utf8.h"

// Helper to translate DOS/BIOS 16-color indices to standard NCurses colors and attributes
static void map_dos_color(uint8_t dos_color, int &ncurses_color, bool &bold)
{
	// DOS color palette (16 colors):
	// 0: Black, 1: Blue, 2: Green, 3: Cyan, 4: Red, 5: Magenta, 6: Brown, 7: Light Gray
	// 8: Dark Gray, 9: Light Blue, 10: Light Green, 11: Light Cyan, 12: Light Red, 13: Light Magenta, 14: Yellow, 15: Bright White
	uint8_t index = dos_color & 0x7;
	bold = (dos_color & 0x8) != 0;

	switch (index) {
		case 0:
			ncurses_color = COLOR_BLACK;
			break;
		case 1:
			ncurses_color = COLOR_BLUE;
			break;
		case 2:
			ncurses_color = COLOR_GREEN;
			break;
		case 3:
			ncurses_color = COLOR_CYAN;
			break;
		case 4:
			ncurses_color = COLOR_RED;
			break;
		case 5:
			ncurses_color = COLOR_MAGENTA;
			break;
		case 6:
			ncurses_color = COLOR_YELLOW;
			break; // Brown/Yellow
		case 7:
			ncurses_color = COLOR_WHITE;
			break;
		default:
			ncurses_color = COLOR_WHITE;
			break;
	}
}

// Helper to allocate and retrieve dynamic color pairs for movie frames
static int get_movie_color_pair(uint8_t dos_fg, uint8_t dos_bg, bool &fg_bold)
{
	static std::map<std::pair<uint8_t, uint8_t>, int> allocated_pairs;
	static int next_pair = 150; // Distinct offset to avoid conflicts with main editor palettes

	// JSON foreground colors are 1-indexed (1-16), background colors are 0-indexed (0-15)
	uint8_t real_fg = (dos_fg > 0) ? (dos_fg - 1) : 0;
	uint8_t real_bg = dos_bg;

	int n_fg = COLOR_WHITE;
	bool bold = false;
	map_dos_color(real_fg, n_fg, bold);
	fg_bold = bold;

	int n_bg = COLOR_BLACK;
	bool bg_bold = false;
	map_dos_color(real_bg, n_bg, bg_bold);

	auto key = std::make_pair(static_cast<uint8_t>(n_fg), static_cast<uint8_t>(n_bg));
	auto it = allocated_pairs.find(key);
	if (it != allocated_pairs.end()) {
		return it->second;
	}

	int pair = next_pair++;
	if (pair < COLOR_PAIRS) {
		init_pair(pair, n_fg, n_bg);
	} else {
		pair = 0;
	}
	allocated_pairs[key] = pair;
	return pair;
}

ui_durmovie::ui_durmovie(std::string name, int x, int y, int width, int height)
    : ui_element(std::move(name), x, y, width, height), last_frame_time_(std::chrono::steady_clock::now())
{
}

ui_durmovie::ui_durmovie(std::string name, int x, int y, int width, int height, const std::string &json_str)
    : ui_element(std::move(name), x, y, width, height), last_frame_time_(std::chrono::steady_clock::now())
{
	load_json(json_str);
}

void ui_durmovie::load_json(const std::string &json_str)
{
	try {
		auto root = nlohmann::json::parse(json_str);
		if (!root.contains("DurMovie")) {
			event_logger::get_instance().log("ui_durmovie: JSON does not contain 'DurMovie' key");
			return;
		}

		const auto &movie = root["DurMovie"];
		framerate_ = movie.value("framerate", 8.0f);
		size_x_ = movie.value("sizeX", 80);
		size_y_ = movie.value("sizeY", 23);

		frames_.clear();
		if (movie.contains("frames") && movie["frames"].is_array()) {
			for (const auto &j_frame : movie["frames"]) {
				durmovie_frame frame;
				frame.frame_number = j_frame.value("frameNumber", 1);
				frame.delay = j_frame.value("delay", 0);

				// Initialize frame cell grid to match movie bounds
				frame.cells.resize(size_y_);
				for (int y = 0; y < size_y_; ++y) {
					frame.cells[y].resize(size_x_);
				}

				// Safely parse contents (rows of glyphs)
				if (j_frame.contains("contents") && j_frame["contents"].is_array()) {
					const auto &contents = j_frame["contents"];
					for (size_t y = 0; y < contents.size() && y < static_cast<size_t>(size_y_); ++y) {
						std::string row_str = contents[y].get<std::string>();
						size_t byte_offset = 0;
						std::string glyph;
						int x = 0;
						while (x < size_x_ && utf8::next_character(row_str, byte_offset, glyph)) {
							frame.cells[y][x].glyph = glyph;
							x++;
						}
					}
				}

				// Safely parse colorMap [col][row][fg/bg]
				if (j_frame.contains("colorMap") && j_frame["colorMap"].is_array()) {
					const auto &color_map = j_frame["colorMap"];
					for (size_t x = 0; x < color_map.size() && x < static_cast<size_t>(size_x_); ++x) {
						const auto &col_colors = color_map[x];
						if (col_colors.is_array()) {
							for (size_t y = 0; y < col_colors.size() && y < static_cast<size_t>(size_y_); ++y) {
								const auto &pair = col_colors[y];
								if (pair.is_array() && pair.size() >= 2) {
									frame.cells[y][x].fg = pair[0].get<uint8_t>();
									frame.cells[y][x].bg = pair[1].get<uint8_t>();
								}
							}
						}
					}
				}
				frames_.push_back(std::move(frame));
			}
		}

		current_frame_ = 0;
		last_frame_time_ = std::chrono::steady_clock::now();
		event_logger::get_instance().log(
		    std::format("ui_durmovie: Loaded movie with {} frames, size {}x{}", frames_.size(), size_x_, size_y_));
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("ui_durmovie: Exception parsing JSON: {}", e.what()));
	}
}

void ui_durmovie::draw(int abs_x, int abs_y) const
{
	if (frames_.empty()) {
		// Draw default placeholder grid if no movie is loaded
		for (int dy = 0; dy < height_; ++dy) {
			move(abs_y + dy, abs_x);
			for (int dx = 0; dx < width_; ++dx) {
				addstr(" ");
			}
		}
		return;
	}

	size_t draw_frame = (state_ == durmovie_state::active) ? current_frame_ : idle_frame_;
	if (draw_frame >= frames_.size()) {
		draw_frame = 0;
	}

	const auto &frame = frames_[draw_frame];

	// Draw frame content to fit widget dimensions, padding empty spaces if out of movie bounds
	for (int dy = 0; dy < height_; ++dy) {
		move(abs_y + dy, abs_x);
		for (int dx = 0; dx < width_; ++dx) {
			if (dy < size_y_ && dx < size_x_) {
				const auto &cell = frame.cells[dy][dx];
				bool bold = false;
				int pair = get_movie_color_pair(cell.fg, cell.bg, bold);

				int attrs = COLOR_PAIR(pair);
				if (bold) {
					attrs |= A_BOLD;
				}

				attron(attrs);
				addstr(cell.glyph.c_str());
				attroff(attrs);
			} else {
				// Fill background with black spaces if widget is larger than movie
				attron(COLOR_PAIR(6)); // drop shadow/black default pair
				addstr(" ");
				attroff(COLOR_PAIR(6));
			}
		}
	}
}

bool ui_durmovie::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	(void)ev;
	(void)abs_x;
	(void)abs_y;
	return false; // Icon is purely visual and does not consume keyboard/mouse events
}

bool ui_durmovie::update_animation()
{
	if (state_ != durmovie_state::active || frames_.size() <= 1) {
		return false;
	}

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_time_).count();

	// Advance frame if time budget has expired based on framerate
	long long frame_duration_ms = static_cast<long long>(1000.0f / framerate_);
	if (elapsed >= frame_duration_ms) {
		current_frame_ = (current_frame_ + 1) % frames_.size();
		last_frame_time_ = now;
		return true; // Redraw requested due to frame transition
	}

	return false;
}

void ui_durmovie::set_state(durmovie_state state)
{
	if (state_ != state) {
		state_ = state;
		if (state_ == durmovie_state::active) {
			current_frame_ = 0;
			last_frame_time_ = std::chrono::steady_clock::now();
		}
	}
}
