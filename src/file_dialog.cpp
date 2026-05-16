#include "file_dialog.h"
#include <algorithm>
#include <ctime>
#include <ncurses.h>
#include <sys/stat.h>
#include "event_logger.h"

file_dialog::file_dialog(const std::string &title, file_dialog_mode mode, bool autocomplete, const std::string &initial_path)
    : dialog(title, 68, 17), mode_(mode), autocomplete_(autocomplete)
{
	if (fs::is_directory(initial_path)) {
		current_path_ = fs::absolute(initial_path);
	} else {
		current_path_ = fs::absolute(fs::path(initial_path).parent_path());
		filename_buffer_ = fs::path(initial_path).filename().string();
	}
	populate_files();
}

void file_dialog::populate_files()
{
	files_.clear();
	selected_index_ = 0;
	scroll_top_ = 0;

	try {
		if (fs::exists(current_path_) && fs::is_directory(current_path_)) {
			if (current_path_.has_parent_path()) {
				file_entry p;
				p.path = current_path_.parent_path();
				p.display_name = "../";
				p.is_dir = true;
				p.size = 0;
				p.mtime = std::chrono::system_clock::now();
				files_.push_back(p);
			}

			std::vector<file_entry> directories;
			std::vector<file_entry> files;

			for (const auto &entry : fs::directory_iterator(current_path_)) {
				if (entry.path().filename().string().starts_with("."))
					continue;

				file_entry fe;
				fe.path = entry.path();
				fe.is_dir = entry.is_directory();

				struct stat attr;
				if (stat(entry.path().string().c_str(), &attr) == 0) {
					fe.mtime = std::chrono::system_clock::from_time_t(attr.st_mtime);
					if (fe.is_dir) {
						fe.size = 0;
					} else {
						fe.size = attr.st_size;
					}
				} else {
					fe.mtime = std::chrono::system_clock::now();
					fe.size = 0;
				}

				if (fe.is_dir) {
					fe.display_name = entry.path().filename().string() + "/";
					directories.push_back(fe);
				} else {
					fe.display_name = entry.path().filename().string();
					files.push_back(fe);
				}
			}

			std::sort(directories.begin(), directories.end(),
				  [](const auto &a, const auto &b) { return a.display_name < b.display_name; });
			std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.display_name < b.display_name; });

			files_.insert(files_.end(), directories.begin(), directories.end());
			files_.insert(files_.end(), files.begin(), files.end());
		}
	} catch (const fs::filesystem_error &e) {
		event_logger::get_instance().log("Filesystem error: " + std::string(e.what()));
	}
}

std::string file_dialog::get_autocomplete_suggestion() const
{
	if (!autocomplete_ || filename_buffer_.empty())
		return "";

	// Check if there is already an exact match, in which case we don't
	// suggest a longer string
	for (const auto &fe : files_) {
		if (!fe.is_dir && fe.display_name == filename_buffer_) {
			return "";
		}
		if (fe.is_dir && fe.display_name == filename_buffer_ + "/") {
			return "";
		}
	}

	std::string suggestion = "";
	std::chrono::system_clock::time_point newest_mtime = std::chrono::system_clock::time_point::min();
	for (const auto &fe : files_) {
		if (!fe.is_dir) {
			if (fe.display_name.starts_with(filename_buffer_)) {
				if (fe.mtime >= newest_mtime) {
					newest_mtime = fe.mtime;
					suggestion = fe.display_name;
				}
			}
		}
	}
	return suggestion;
}

void file_dialog::draw_button(int by, int bx, const std::string &btext, char bhot, bool focused) const
{
	attron(COLOR_PAIR(1));
	mvaddstr(by, bx + btext.length(), "▄");
	for (size_t i = 0; i < btext.length(); ++i)
		mvaddstr(by + 1, bx + 1 + i, "▀");
	attroff(COLOR_PAIR(1));

	move(by, bx);
	if (focused) {
		attrset(COLOR_PAIR(14));
	} else {
		attrset(COLOR_PAIR(10));
	}
	for (size_t i = 0; i < btext.length(); ++i) {
		if (std::tolower(btext[i]) == std::tolower(bhot)) {
			attrset(COLOR_PAIR(15));
			addch(btext[i]);
			if (focused) {
				attrset(COLOR_PAIR(14));
			} else {
				attrset(COLOR_PAIR(10));
			}
		} else
			addch(btext[i]);
	}
}

void file_dialog::draw() const
{
	dialog::draw();

	// Name Label
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 2, x_ + 2, "Name");
	attrset(COLOR_PAIR(16)); // Yellow hotkey
	mvaddch(y_ + 2, x_ + 2, 'N');
	if (focus_ == focus_element::entry_box) {
		attrset(COLOR_PAIR(11)); // Bright white if focused
		mvaddstr(y_ + 2, x_ + 3, "ame");
	} else {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + 2, x_ + 3, "ame");
	}

	// Entry Box
	attrset(COLOR_PAIR(5)); // Bright White on Blue
	move(y_ + 2, x_ + 8);
	for (int i = 0; i < 40; ++i)
		addch(' ');
	mvaddstr(y_ + 2, x_ + 8, filename_buffer_.c_str());

	// Autocomplete suggestion
	if (focus_ == focus_element::entry_box) {
		std::string suggestion = get_autocomplete_suggestion();
		if (!suggestion.empty() && suggestion.length() > filename_buffer_.length()) {
			attrset(COLOR_PAIR(4)); // Cyan on Blue as fallback for "gray on blue"
			mvaddstr(y_ + 2, x_ + 8 + filename_buffer_.length(), suggestion.c_str() + filename_buffer_.length());
		}
	}

	// History Button
	bool hist_focus = focus_ == focus_element::history_btn;
	if (hist_focus) {
		attrset(COLOR_PAIR(14));
	} else {
		attrset(COLOR_PAIR(10));
	} // Green
	mvaddch(y_ + 2, x_ + 49, 'v');

	// Files Label
	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + 4, x_ + 2, "Files");
	attrset(COLOR_PAIR(16)); // Yellow hotkey
	mvaddch(y_ + 4, x_ + 2, 'F');
	if (focus_ == focus_element::file_view) {
		attrset(COLOR_PAIR(11));
		mvaddstr(y_ + 4, x_ + 3, "iles");
	} else {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + 4, x_ + 3, "iles");
	}

	// Filesystem View
	int list_box_y = y_ + 5;
	int list_box_height = 8; // Lines 5 to 12
	int list_box_w = 46;
	int col_width = 22;

	for (int i = 0; i < list_box_height; ++i) {
		move(list_box_y + i, x_ + 2);
		attrset(COLOR_PAIR(17)); // Black on Cyan
		for (int j = 0; j < list_box_w; ++j)
			addch(' ');
		mvaddstr(list_box_y + i, x_ + 2 + col_width, "│");
	}

	int files_height = list_box_height - 1;
	for (int i = 0; i < files_height; ++i) {
		for (int col = 0; col < 2; ++col) {
			int file_idx = scroll_top_ + i + (col * files_height);
			if (file_idx < static_cast<int>(files_.size())) {
				bool is_sel = (file_idx == selected_index_);
				bool in_view = (focus_ == focus_element::file_view);

				if (is_sel && in_view)
					attrset(COLOR_PAIR(18)); // Bright Yellow on Cyan
				else
					attrset(COLOR_PAIR(17));

				int draw_col_width;
				if (col == 0) {
					draw_col_width = col_width;
				} else {
					draw_col_width = (list_box_w - col_width - 1);
				}
				std::string name = files_[file_idx].display_name;
				if (name.length() > (size_t)draw_col_width) {
					name = name.substr(0, draw_col_width);
				}
				int draw_x = x_ + 2 + (col * (col_width + 1));
				mvaddstr(list_box_y + i, draw_x, name.c_str());
			}
		}
	}

	// Scrollbar
	attrset(COLOR_PAIR(17));
	move(list_box_y + files_height, x_ + 2);
	addstr("◄");
	for (int j = 1; j < list_box_w - 1; ++j)
		addstr("░");
	addstr("►");
	if (!files_.empty()) {
		int max_scroll = std::max(1, static_cast<int>(files_.size()) - files_height * 2);
		int thumb_pos = 1;
		if (max_scroll > 0) {
			thumb_pos = 1 + (scroll_top_ * (list_box_w - 3)) / max_scroll;
		}
		if (thumb_pos >= list_box_w - 1)
			thumb_pos = list_box_w - 2;
		mvaddstr(list_box_y + files_height, x_ + 2 + thumb_pos, "■");
	}

	// Buttons
	std::string primary_btn_text;
	if (mode_ == file_dialog_mode::open) {
		primary_btn_text = "   Ok   ";
	} else {
		primary_btn_text = "   Ok   ";
	}
	draw_button(y_ + 2, x_ + 53, primary_btn_text, 'o', focus_ == focus_element::ok_btn);
	draw_button(y_ + 5, x_ + 53, " Cancel ", 'c', focus_ == focus_element::cancel_btn);

	// Info Section
	int info_y = y_ + height_ - 3;
	attrset(COLOR_PAIR(5)); // Bright White on Blue (or Bright Blue on Dark Blue)
	for (int i = 0; i < 2; ++i) {
		move(info_y + i, x_ + 1);
		for (int j = 0; j < width_ - 2; ++j)
			addch(' ');
	}

	std::string path_str = current_path_.string();
	if (path_str.length() > (size_t)width_ - 4) {
		path_str = path_str.substr(path_str.length() - (width_ - 4));
	}
	mvaddstr(info_y, x_ + 2, path_str.c_str());

	std::string active_name = filename_buffer_;
	if (focus_ == focus_element::entry_box) {
		std::string suggestion = get_autocomplete_suggestion();
		if (!suggestion.empty()) {
			active_name = suggestion;
		}
	} else if (focus_ == focus_element::file_view && selected_index_ < static_cast<int>(files_.size())) {
		active_name = files_[selected_index_].display_name;
	}
	if (!active_name.empty()) {
		std::string info_str = active_name;
		for (const auto &fe : files_) {
			if (fe.display_name == active_name ||
			    (fe.display_name.substr(0, fe.display_name.length() - 1) == active_name && fe.is_dir)) {
				info_str += "  " + std::to_string(fe.size);

				std::time_t t = std::chrono::system_clock::to_time_t(fe.mtime);
				std::tm *tm = std::localtime(&t);
				char time_buf[64];
				std::strftime(time_buf, sizeof(time_buf), "%b %e, %Y %I:%M%p", tm);
				info_str += "  ";
				info_str += time_buf;
				break;
			}
		}
		mvaddstr(info_y + 1, x_ + 2, info_str.c_str());
	}

	// History Dropdown Overlay
	if (history_dropdown_open_) {
		int drop_y = y_ + 3;
		int drop_x = x_ + 8;
		int drop_w = 40;
		int drop_h = std::min(5, static_cast<int>(file_history_.size()));

		for (int i = 0; i < drop_h; ++i) {
			move(drop_y + i, drop_x);
			if (i == history_sel_idx_) {
				attrset(COLOR_PAIR(19));
			} else {
				attrset(COLOR_PAIR(1));
			}
			for (int j = 0; j < drop_w; ++j)
				addch(' ');
			std::string h_str = file_history_[i];
			if (h_str.length() > (size_t)drop_w - 1)
				h_str = h_str.substr(0, drop_w - 1);
			mvaddstr(drop_y + i, drop_x, h_str.c_str());
		}
	}

	// Cursor
	if (focus_ == focus_element::entry_box && !history_dropdown_open_) {
		move(y_ + 2, x_ + 8 + filename_buffer_.length());
		curs_set(1);
	} else {
		curs_set(0);
	}

	attrset(0);
}

dialog_result file_dialog::handle_key(int key)
{
	if (key == 27) { // Esc
		if (history_dropdown_open_) {
			history_dropdown_open_ = false;
			return dialog_result::pending;
		}
		return dialog_result::cancelled;
	}

	if (history_dropdown_open_) {
		if (key == KEY_UP && history_sel_idx_ > 0)
			history_sel_idx_--;
		else if (key == KEY_DOWN && history_sel_idx_ < static_cast<int>(file_history_.size()) - 1)
			history_sel_idx_++;
		else if (key == KEY_ENTER || key == 10 || key == 13) {
			if (!file_history_.empty() && history_sel_idx_ >= 0 && history_sel_idx_ < static_cast<int>(file_history_.size())) {
				filename_buffer_ = file_history_[history_sel_idx_];
			}
			history_dropdown_open_ = false;
			focus_ = focus_element::entry_box;
		}
		return dialog_result::pending;
	}

	int files_height = 7; // List box has 8 lines, 7 for files

	if (key == '\t' || key == KEY_BTAB) {
		int num_elements = 5;
		int current = static_cast<int>(focus_);
		if (key == '\t')
			current = (current + 1) % num_elements;
		else
			current = (current - 1 + num_elements) % num_elements;
		focus_ = static_cast<focus_element>(current);
		return dialog_result::pending;
	}

	if (focus_ == focus_element::entry_box) {
		if (key >= 32 && key <= 126)
			filename_buffer_ += static_cast<char>(key);
		else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!filename_buffer_.empty())
				filename_buffer_.pop_back();
		} else if (key == KEY_RIGHT) {
			std::string suggestion = get_autocomplete_suggestion();
			if (!suggestion.empty())
				filename_buffer_ = suggestion;
		} else if (key == KEY_ENTER || key == 10 || key == 13) {
			if (filename_buffer_.empty())
				return dialog_result::pending;

			std::string suggestion = get_autocomplete_suggestion();
			if (!suggestion.empty()) {
				filename_buffer_ = suggestion;
			}

			fs::path entered_path = current_path_ / filename_buffer_;
			if (fs::exists(entered_path) && fs::is_directory(entered_path)) {
				current_path_ = fs::canonical(entered_path);
				populate_files();
				filename_buffer_.clear();
			} else {
				if (std::find(file_history_.begin(), file_history_.end(), filename_buffer_) == file_history_.end()) {
					file_history_.insert(file_history_.begin(), filename_buffer_);
				}
				return dialog_result::confirmed;
			}
		} else if (key == KEY_DOWN) {
			if (!file_history_.empty()) {
				history_dropdown_open_ = true;
				history_sel_idx_ = 0;
			}
		}
	} else if (focus_ == focus_element::history_btn) {
		if (key == ' ' || key == KEY_ENTER || key == 10 || key == 13) {
			if (!file_history_.empty()) {
				history_dropdown_open_ = true;
				history_sel_idx_ = 0;
			}
		}
	} else if (focus_ == focus_element::file_view) {
		switch (key) {
			case KEY_UP:
				if (selected_index_ > 0)
					selected_index_--;
				break;
			case KEY_DOWN:
				if (selected_index_ < static_cast<int>(files_.size()) - 1)
					selected_index_++;
				break;
			case KEY_LEFT:
				if (selected_index_ >= files_height)
					selected_index_ -= files_height;
				break;
			case KEY_RIGHT:
				if (selected_index_ + files_height < static_cast<int>(files_.size()))
					selected_index_ += files_height;
				else
					selected_index_ = static_cast<int>(files_.size()) - 1;
				break;
			case KEY_ENTER:
			case 10:
			case 13:
				if (selected_index_ < static_cast<int>(files_.size())) {
					const auto &entry = files_[selected_index_];
					if (entry.is_dir) {
						current_path_ = fs::canonical(entry.path);
						populate_files();
						filename_buffer_.clear();
					} else {
						filename_buffer_ = entry.display_name;
						focus_ = focus_element::entry_box;
					}
				}
				break;
		}

		while (selected_index_ < scroll_top_) {
			scroll_top_ -= files_height;
		}
		while (selected_index_ >= scroll_top_ + files_height * 2) {
			scroll_top_ += files_height;
		}

		if (selected_index_ < static_cast<int>(files_.size())) {
			filename_buffer_ = files_[selected_index_].display_name;
		}
	} else if (focus_ == focus_element::ok_btn) {
		if (key == ' ' || key == KEY_ENTER || key == 10 || key == 13) {
			if (!filename_buffer_.empty()) {
				fs::path entered_path = current_path_ / filename_buffer_;
				if (fs::is_directory(entered_path)) {
					current_path_ = fs::canonical(entered_path);
					populate_files();
					filename_buffer_.clear();
					focus_ = focus_element::entry_box;
				} else
					return dialog_result::confirmed;
			}
		}
	} else if (focus_ == focus_element::cancel_btn) {
		if (key == ' ' || key == KEY_ENTER || key == 10 || key == 13)
			return dialog_result::cancelled;
	}

	return dialog_result::pending;
}

std::string file_dialog::get_result() const
{
	return (current_path_ / filename_buffer_).string();
}
