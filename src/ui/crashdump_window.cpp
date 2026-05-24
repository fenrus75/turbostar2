#include "crashdump_window.h"
#include <ncurses.h>

crashdump_window::crashdump_window(int id, int x, int y, int width, int height) : window(id, x, y, width, height, "Crashdumps")
{
	set_background_color_pair(2); // Use standard window background

	listbox_ = std::make_unique<ui_listbox>(
	    "crashdumps", 0, 0, width_ - 2, 5,
	    [this](int new_index) {
		    if (new_index != last_selected_index_) {
			    detail_scroll_offset_ = 0;
			    last_selected_index_ = new_index;
			    invalidate();
		    }
	    },
	    nullptr // no submit action
	);

	populate_listbox();
}

void crashdump_window::populate_listbox()
{
	current_dumps_ = crashdump_manager::get_instance().get_crashdumps();
	std::vector<std::string> display_items;

	for (const auto &dump : current_dumps_) {
		display_items.push_back(" " + dump.crash_id + " | " + dump.timestamp + " | " + dump.signal + " | " + dump.executable);
	}

	listbox_->set_items(display_items);
}

void crashdump_window::draw_content() const
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
		listbox_->draw(0, 0);
	}

	current_y += list_height;

	// Draw separator
	for (int i = 0; i < max_width; ++i) {
		mvaddch(current_y, start_x + i, ACS_HLINE);
	}
	current_y++;

	// Draw details
	int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
	if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
		const auto &dump = current_dumps_[selected_idx];

		// Split raw_info by newline
		std::vector<std::string> lines;
		size_t start = 0;
		const std::string &raw = dump.raw_info;
		while (start < raw.length()) {
			size_t end = raw.find('\n', start);
			if (end == std::string::npos) {
				lines.push_back(raw.substr(start));
				break;
			}
			lines.push_back(raw.substr(start, end - start));
			start = end + 1;
		}

		// Draw lines with scrolling
		int detail_height = (y_ + height_ - 1) - current_y;
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
		mvprintw(current_y, start_x, " No crashdump selected.");
	}

	attroff(COLOR_PAIR(get_background_color_pair()));
}

bool crashdump_window::process_events()
{
	bool needs_render = false;

	while (auto ev = get_window_queue().pop()) {
		if (ev->type == event_type::key_press && is_active()) {
			int key = ev->key_code;

			// Handle details scrolling
			if (key == 21) { // Ctrl-U (Page up)
				detail_scroll_offset_ -= 10;
				if (detail_scroll_offset_ < 0)
					detail_scroll_offset_ = 0;
				needs_render = true;
			} else if (key == 22) { // Ctrl-V (Page down)
				int selected_idx = listbox_ ? listbox_->get_selected_index() : -1;
				if (selected_idx >= 0 && selected_idx < (int)current_dumps_.size()) {
					detail_scroll_offset_ += 10;
					// Could add logic to prevent scrolling past end, but let's keep it simple
					needs_render = true;
				}
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
		// Let listbox set cursor if it wants, though it's a read-only list
		listbox_->draw(0, 0);
	}
}