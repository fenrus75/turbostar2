#include "crashdump_window.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
namespace fs = std::filesystem;
#include <ncurses.h>
#include "project_manager.h"
#include "fs_utils.h"
#include "markdown_utils.h"

crashdump_window::crashdump_window(int id, int x, int y, int width, int height, event_queue &global_queue)
    : window(id, x, y, width, height, "Crashdumps"), global_queue_(global_queue)
{
	set_background_color_pair(3); // Use yellow-on-blue window background

	listbox_ = std::make_unique<ui_listbox>(
	    "crashdumps", 0, 0, width_ - 2, 5,
	    [this](int new_index) {
		    if (new_index != last_selected_index_) {
			    detail_scroll_offset_ = 0;
			    last_selected_index_ = new_index;
			    invalidate();
		    }
	    },
	    [this](int /*index*/) { go_to_source(); });

	populate_listbox();
}

void crashdump_window::populate_listbox()
{
	crashdump_manager::get_instance().refresh("");
	current_dumps_ = crashdump_manager::get_instance().get_crashdumps();
	std::vector<std::string> display_items;

	for (const auto &dump : current_dumps_) {
		display_items.push_back(" " + dump.crash_id + " | " + dump.timestamp + " | " + dump.signal + " | " + dump.executable);
	}

	listbox_->set_items(display_items);
}

void crashdump_window::draw_content(bool /*cursor_only*/) const
{
	if (!is_visible())
		return;

	int list_height = height_ * 0.3; // Top 30% for list
	if (list_height < 3)
		list_height = 3;

	int current_y = y_ + 1;
	int start_x = x_ + 1;
	int max_width = width_ - 2;

	attron(COLOR_PAIR(get_background_color_pair()));

	// Clear content area
	for (int i = 1; i < height_ - 1; ++i) {
		move(y_ + i, x_ + 1);
		for (int j = 0; j < width_ - 2; ++j)
			addch(' ');
	}

	if (listbox_) {
		listbox_->set_bounds(start_x, current_y, max_width, list_height);
		listbox_->set_focus(is_active());
		// Draw the listbox at its absolute screen coordinates.
		listbox_->draw(listbox_->x(), listbox_->y());
	}

	// Restore window background color pair since listbox draw changes/turns off attributes
	attron(COLOR_PAIR(get_background_color_pair()));

	current_y += list_height;

	// Draw separator
	for (int i = 0; i < max_width; ++i) {
		mvaddch(current_y, start_x + i, ACS_HLINE);
	}
	current_y++;
	details_top_y_ = current_y;

	// Draw details
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
		const auto &dump = current_dumps_[selected_idx];

		// Align markdown tables in raw_info first with framed borders to fit window max_width
		std::string raw = markdown_utils::align_all_tables(dump.raw_info, true, 0, max_width);

		// Split raw by newline
		std::vector<std::string> lines;
		size_t start = 0;
		while (start < raw.length()) {
			size_t end = raw.find('\n', start);
			if (end == std::string::npos) {
				lines.push_back(raw.substr(start));
				break;
			}
			lines.push_back(raw.substr(start, end - start));
			start = end + 1;
		}

		int detail_height = (y_ + height_ - 1) - current_y;
		detail_view_height_ = detail_height;
		total_detail_lines_ = static_cast<int>(lines.size());

		int max_scroll = std::max(0, total_detail_lines_ - detail_view_height_);
		detail_scroll_offset_ = std::clamp(detail_scroll_offset_, 0, max_scroll);

		// Draw lines with scrolling
		for (int i = 0; i < detail_height; ++i) {
			int line_idx = detail_scroll_offset_ + i;
			if (line_idx < (int)lines.size()) {
				std::string display_line = lines[line_idx];
				if (display_line.length() > static_cast<size_t>(max_width)) {
					display_line = display_line.substr(0, max_width);
				}
				mvprintw(current_y + i, start_x, "%s", display_line.c_str());
			}
		}
	} else {
		total_detail_lines_ = 0;
		detail_view_height_ = (y_ + height_ - 1) - current_y;
		mvprintw(current_y, start_x, " No crashdump selected.");
	}

	attroff(COLOR_PAIR(get_background_color_pair()));
}

void crashdump_window::draw_border() const
{
	::window::draw_border();

	// Draw details scrollbar if detail lines exceed view height
	int max_scroll = std::max(0, total_detail_lines_ - detail_view_height_);
	if (total_detail_lines_ > detail_view_height_ && details_top_y_ > y_) {
		int track_top = details_top_y_;
		int track_bottom = y_ + height_ - 2;
		int track_height = track_bottom - track_top + 1;

		if (track_height >= 3) {
			attron(COLOR_PAIR(4));
			for (int y = track_top; y <= track_bottom; ++y) {
				mvaddstr(y, x_ + width_ - 1, "▒");
			}
			mvaddstr(track_top, x_ + width_ - 1, "▲");
			mvaddstr(track_bottom, x_ + width_ - 1, "▼");

			int thumb_range = track_height - 2;
			if (thumb_range > 0 && max_scroll > 0) {
				double ratio = static_cast<double>(detail_scroll_offset_) / max_scroll;
				int thumb_pos = static_cast<int>(ratio * (thumb_range - 1) + 0.5);
				thumb_pos = std::clamp(thumb_pos, 0, thumb_range - 1);
				mvaddstr(track_top + 1 + thumb_pos, x_ + width_ - 1, "█");
			}
			attroff(COLOR_PAIR(4));
		}
	}

	// Draw "Go to Source" button on the bottom border
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
		const auto &dump = current_dumps_[selected_idx];
		int btn_x = 2; // relative to window
		attrset(COLOR_PAIR(is_active() ? 5 : 38));
		mvaddstr(y_ + height_ - 1, x_ + btn_x, "[");
		attrset(COLOR_PAIR(10));
		addstr("Go to Source");
		attrset(COLOR_PAIR(is_active() ? 5 : 38));
		addstr("]");

		if (has_coredump(dump.crash_id)) {
			int btn2_x = 18;
			attrset(COLOR_PAIR(is_active() ? 5 : 38));
			mvaddstr(y_ + height_ - 1, x_ + btn2_x, "[");
			attrset(COLOR_PAIR(10));
			addstr("Debug Coredump");
			attrset(COLOR_PAIR(is_active() ? 5 : 38));
			addstr("]");
		}
	}
}

bool crashdump_window::process_events()
{
	bool needs_render = false;
	int max_scroll = std::max(0, total_detail_lines_ - detail_view_height_);

	while (auto ev = get_window_queue().pop()) {
		if (ev->type == event_type::key_press && is_active()) {
			int key = ev->key_code;

			// Handle details scrolling
			if (key == 21 || key == KEY_PPAGE) { // Ctrl-U or PageUp
				int page = std::max(1, detail_view_height_ - 2);
				detail_scroll_offset_ = std::clamp(detail_scroll_offset_ - page, 0, max_scroll);
				needs_render = true;
			} else if (key == 22 || key == KEY_NPAGE) { // Ctrl-V or PageDown
				int page = std::max(1, detail_view_height_ - 2);
				detail_scroll_offset_ = std::clamp(detail_scroll_offset_ + page, 0, max_scroll);
				needs_render = true;
			} else if (key == KEY_UP) {
				detail_scroll_offset_ = std::clamp(detail_scroll_offset_ - 1, 0, max_scroll);
				needs_render = true;
			} else if (key == KEY_DOWN) {
				detail_scroll_offset_ = std::clamp(detail_scroll_offset_ + 1, 0, max_scroll);
				needs_render = true;
			} else if (key == 's' || key == 'S' || key == 'g' || key == 'G') {
				go_to_source();
				needs_render = true;
			} else if (key == 'd' || key == 'D') {
				debug_coredump();
				needs_render = true;
			} else if (listbox_ && listbox_->handle_event(*ev, 0, 0)) {
				needs_render = true;
			}
		} else if (ev->type == event_type::mouse_scroll_up) {
			detail_scroll_offset_ = std::clamp(detail_scroll_offset_ - 3, 0, max_scroll);
			needs_render = true;
		} else if (ev->type == event_type::mouse_scroll_down) {
			detail_scroll_offset_ = std::clamp(detail_scroll_offset_ + 3, 0, max_scroll);
			needs_render = true;
		} else if (ev->type == event_type::mouse_click) {
			if (ev->mouse_y == y_ + height_ - 1) {
				int click_x = ev->mouse_x - x_;
				int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
				if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
					if (click_x >= 2 && click_x < 16) {
						go_to_source();
						needs_render = true;
					} else if (click_x >= 18 && click_x < 34) {
						debug_coredump();
						needs_render = true;
					}
				}
			} else if (ev->mouse_x == x_ + width_ - 1 && ev->mouse_y >= details_top_y_ && ev->mouse_y <= y_ + height_ - 2) {
				int track_top = details_top_y_;
				int track_bottom = y_ + height_ - 2;
				if (ev->mouse_y == track_top) {
					detail_scroll_offset_ = std::clamp(detail_scroll_offset_ - 1, 0, max_scroll);
				} else if (ev->mouse_y == track_bottom) {
					detail_scroll_offset_ = std::clamp(detail_scroll_offset_ + 1, 0, max_scroll);
				} else if (max_scroll > 0 && (track_bottom - track_top - 1) > 0) {
					int rel_y = ev->mouse_y - (track_top + 1);
					int thumb_range = track_bottom - track_top - 1;
					double ratio = static_cast<double>(rel_y) / thumb_range;
					detail_scroll_offset_ = std::clamp(static_cast<int>(ratio * max_scroll + 0.5), 0, max_scroll);
				}
				needs_render = true;
			} else if (listbox_ && listbox_->handle_event(*ev, 0, 0)) {
				needs_render = true;
			}
		}
	}

	if (needs_render)
		invalidate();
	return needs_render;
}

void crashdump_window::set_cursor_position() const
{
	if (is_active() && listbox_) {
		// Let listbox set cursor if it wants, though it's a read-only list.
		// Align the virtual cursor coordinates with the listbox's absolute bounds.
		listbox_->set_cursor_position(listbox_->x(), listbox_->y());
	}
}

void crashdump_window::go_to_source()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
		const auto &dump = current_dumps_[selected_idx];
		auto loc = get_crash_location(dump);
		if (loc) {
			editor_event open_ev;
			open_ev.type = event_type::open_file;
			open_ev.payload = loc->first + ":" + std::to_string(loc->second);
			global_queue_.push(open_ev);
		}
	}
}

std::optional<std::pair<std::string, int>> crashdump_window::get_crash_location(const crashdump_info &dump) const
{
	// 1. Check for Failed Assertion
	size_t assert_pos = dump.raw_info.find("### Failed Assertion");
	if (assert_pos != std::string::npos) {
		size_t file_pos = dump.raw_info.find("File: ", assert_pos);
		size_t line_pos = dump.raw_info.find("Line: ", assert_pos);
		if (file_pos != std::string::npos && line_pos != std::string::npos) {
			size_t file_start = file_pos + 6;
			size_t file_end = dump.raw_info.find('\n', file_start);
			std::string file_path = dump.raw_info.substr(file_start, file_end - file_start);

			size_t line_start = line_pos + 6;
			size_t line_end = dump.raw_info.find('\n', line_start);
			std::string line_str = dump.raw_info.substr(line_start, line_end - line_start);

			while (!file_path.empty() && std::isspace(file_path.back()))
				file_path.pop_back();
			while (!file_path.empty() && std::isspace(file_path.front()))
				file_path.erase(file_path.begin());
			while (!line_str.empty() && std::isspace(line_str.back()))
				line_str.pop_back();
			while (!line_str.empty() && std::isspace(line_str.front()))
				line_str.erase(line_str.begin());

			try {
				int line_num = std::stoi(line_str);
				std::string proj_root = project_manager::get_instance().get_project_root();
				std::filesystem::path p(file_path);
				if (!p.is_absolute()) {
					p = std::filesystem::path(proj_root) / p;
				}
				std::string abs_file = p.lexically_normal().string();
				if (std::filesystem::exists(abs_file)) {
					return std::make_pair(abs_file, line_num);
				}
			} catch (...) {
			}
		}
	}

	// 2. Scan Backtrace from top to bottom
	std::string proj_root = project_manager::get_instance().get_project_root();
	size_t current = 0;
	while (current < dump.raw_info.length()) {
		size_t next_line = dump.raw_info.find('\n', current);
		std::string line;
		if (next_line == std::string::npos) {
			line = dump.raw_info.substr(current);
			current = dump.raw_info.length();
		} else {
			line = dump.raw_info.substr(current, next_line - current);
			current = next_line + 1;
		}

		if (line.starts_with('|')) {
			std::vector<std::string> parts;
			size_t start_p = 0;
			while (start_p < line.length()) {
				size_t end_p = line.find('|', start_p);
				if (end_p == std::string::npos) {
					parts.push_back(line.substr(start_p));
					break;
				}
				parts.push_back(line.substr(start_p, end_p - start_p));
				start_p = end_p + 1;
			}

			if (parts.size() >= 5) {
				std::string location = parts[4];
				while (!location.empty() && std::isspace(location.back()))
					location.pop_back();
				while (!location.empty() && std::isspace(location.front()))
					location.erase(location.begin());

				if (!location.empty() && location != "Location" && location[0] != '?' && !location.starts_with("[")) {
					size_t colon = location.find_last_of(':');
					if (colon != std::string::npos) {
						std::string file_part = location.substr(0, colon);
						std::string line_part = location.substr(colon + 1);

						if (file_part != "src/crash_catcher/crash_catcher.c" &&
						    !file_part.starts_with("src/crash_catcher/")) {
							try {
								int line_num = std::stoi(line_part);
								std::filesystem::path p(file_part);
								if (!p.is_absolute()) {
									p = std::filesystem::path(proj_root) / p;
								}
								std::string abs_file = p.lexically_normal().string();
								if (std::filesystem::exists(abs_file)) {
									return std::make_pair(abs_file, line_num);
								}
							} catch (...) {
							}
						}
					}
				}
			}
		}
	}

	return std::nullopt;
}

std::string crashdump_window::get_displayed_title() const
{
	std::string dump_dir = fs_utils::get_project_dump_dir();
	const char *home = std::getenv("HOME");
	if (home && dump_dir.starts_with(home)) {
		dump_dir = "~" + dump_dir.substr(std::strlen(home));
	}
	return "Crashdumps: " + dump_dir;
}

bool crashdump_window::update_viewport() const
{
	bool changed = (detail_scroll_offset_ != last_detail_scroll_offset_);
	last_detail_scroll_offset_ = detail_scroll_offset_;
	return changed;
}

bool crashdump_window::has_coredump(const std::string &crash_id) const
{
	fs::path dump_dir = fs::path(fs_utils::get_project_dump_dir()) / ("crash_" + crash_id);
	if (!fs::exists(dump_dir))
		return false;
	for (const auto &entry : fs::directory_iterator(dump_dir)) {
		std::string filename = entry.path().filename().string();
		if (filename.starts_with("core")) {
			return true;
		}
	}
	static bool has_coredumpctl = (std::system("which coredumpctl >/dev/null 2>&1") == 0);
	return has_coredumpctl;
}

void crashdump_window::debug_coredump()
{
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
		const auto &dump = current_dumps_[selected_idx];
		if (has_coredump(dump.crash_id)) {
			editor_event debug_ev;
			debug_ev.type = event_type::agent_start_coredump_gdb;
			debug_ev.payload = dump.crash_id;
			global_queue_.push(debug_ev);
		}
	}
}