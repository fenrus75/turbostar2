#include "file_dialog.h"
#include "event_logger.h"
#include <ncurses.h>
#include <algorithm>

file_dialog::file_dialog(const std::string& title, file_dialog_mode mode, const std::string& initial_path)
	: dialog(title, 68, 16), mode_(mode)
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
				files_.push_back("..");
			}

			std::vector<fs::path> directories;
			std::vector<fs::path> files;

			for (const auto& entry : fs::directory_iterator(current_path_)) {
				if (entry.path().filename().string().starts_with(".")) continue;
				if (entry.is_directory()) {
					directories.push_back(entry.path());
				} else {
					files.push_back(entry.path());
				}
			}

			std::sort(directories.begin(), directories.end());
			std::sort(files.begin(), files.end());

			files_.insert(files_.end(), directories.begin(), directories.end());
			files_.insert(files_.end(), files.begin(), files.end());
		}
	} catch (const fs::filesystem_error& e) {
		event_logger::get_instance().log("Filesystem error: " + std::string(e.what()));
	}
}

void file_dialog::draw() const
{
	dialog::draw();
	attrset(COLOR_PAIR(1));
	
	mvaddstr(y_ + 2, x_ + 2, "Name");
	attrset(focus_idx_ == 0 ? COLOR_PAIR(19) : COLOR_PAIR(3));
	move(y_ + 2, x_ + 8);
	for (int i = 0; i < 56; ++i) addch(' ');
	mvaddstr(y_ + 2, x_ + 8, filename_buffer_.c_str());
	if (focus_idx_ == 0) {
		move(y_ + 2, x_ + 8 + filename_buffer_.length());
		curs_set(1);
	} else {
		curs_set(0);
	}

	attrset(COLOR_PAIR(11));
	mvaddstr(y_ + 3, x_ + 1, "├");
	for(int i=0; i<width_-2; ++i) addstr("─");
	addstr("┤");
	
	int list_box_y = y_ + 4;
	int list_box_height = height_ - 8;
	int col_width = (width_ - 4) / 2;

	for (int i = 0; i < list_box_height; ++i) {
		for (int col = 0; col < 2; ++col) {
			int file_idx = scroll_top_ + i + (col * list_box_height);
			move(list_box_y + i, x_ + 2 + (col * col_width));
			attrset(COLOR_PAIR(1));
			for (int j=0; j < col_width; ++j) addch(' ');

			if (file_idx < static_cast<int>(files_.size())) {
				std::string name = files_[file_idx].filename().string();
				if (name == "..") name = "[..]";
				else if (fs::is_directory(files_[file_idx])) name = "[" + name + "]";

				if (file_idx == selected_index_ && focus_idx_ == 1) attrset(COLOR_PAIR(19));
				else attrset(COLOR_PAIR(1));
				
				mvaddstr(list_box_y + i, x_ + 2 + (col * col_width), name.c_str());
			}
		}
	}

	attrset(COLOR_PAIR(11));
	mvaddstr(y_ + height_ - 4, x_ + 1, "├");
	for(int i=0; i<width_-2; ++i) addstr("─");
	addstr("┤");

	auto draw_btn = [&](int by, int bx, const std::string& btext, char bhot, bool focused) {
		attrset(COLOR_PAIR(1));
		mvaddstr(y_ + by, x_ + bx + btext.length(), "▄");
		for (size_t i = 0; i < btext.length(); ++i) mvaddstr(y_ + by + 1, x_ + bx + 1 + i, "▀");
		move(y_ + by, x_ + bx);
		attrset(focused ? COLOR_PAIR(14) : COLOR_PAIR(10));
		for (size_t i = 0; i < btext.length(); ++i) {
			if (std::tolower(btext[i]) == std::tolower(bhot)) {
				attrset(COLOR_PAIR(15)); addch(btext[i]); attrset(focused ? COLOR_PAIR(14) : COLOR_PAIR(10));
			} else addch(btext[i]);
		}
	};

	std::string primary_btn_text = (mode_ == file_dialog_mode::open) ? " Open " : " Save ";
	char primary_hotkey = (mode_ == file_dialog_mode::open) ? 'o' : 's';
	int btn_y = y_ + height_ - 3;
	draw_btn(btn_y, x_ + 10, primary_btn_text, primary_hotkey, focus_idx_ == 2);
	draw_btn(btn_y, x_ + 25, " Cancel ", 'c', focus_idx_ == 3);

	attrset(COLOR_PAIR(1));
	mvaddstr(y_ + height_ - 2, x_ + 2, current_path_.c_str());
	
	attrset(0);
}

dialog_result file_dialog::handle_key(int key)
{
	if (key == 27) return dialog_result::cancelled;
	int list_box_height = height_ - 8;

	if (key == '	' || key == KEY_BTAB) {
		if (key == '	') focus_idx_ = (focus_idx_ + 1) % 4;
		else focus_idx_ = (focus_idx_ - 1 + 4) % 4;
		return dialog_result::pending;
	}

	if (focus_idx_ == 0) {
		if (key >= 32 && key <= 126) filename_buffer_ += static_cast<char>(key);
		else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
			if (!filename_buffer_.empty()) filename_buffer_.pop_back();
		} else if (key == KEY_ENTER || key == 10 || key == 13) {
			fs::path entered_path = current_path_ / filename_buffer_;
			if (fs::is_directory(entered_path)) {
				current_path_ = fs::canonical(entered_path);
				populate_files();
				filename_buffer_.clear();
			} else return dialog_result::confirmed;
		}
	} else if (focus_idx_ == 1) {
		switch (key) {
			case KEY_UP:
				if (selected_index_ > 0) selected_index_--;
				break;
			case KEY_DOWN:
				if (selected_index_ < static_cast<int>(files_.size()) - 1) selected_index_++;
				break;
			case KEY_LEFT:
				if (selected_index_ >= list_box_height) selected_index_ -= list_box_height;
				break;
			case KEY_RIGHT:
				if (selected_index_ + list_box_height < static_cast<int>(files_.size())) selected_index_ += list_box_height;
				else selected_index_ = static_cast<int>(files_.size()) - 1;
				break;
			case KEY_ENTER: case 10: case 13:
				if (selected_index_ < static_cast<int>(files_.size())) {
					const auto& path = files_[selected_index_];
					if (fs::is_directory(path)) {
						current_path_ = fs::canonical(path);
						populate_files();
						filename_buffer_.clear();
					} else {
						filename_buffer_ = path.filename().string();
						focus_idx_ = 0;
					}
				}
				break;
		}
		if (selected_index_ < scroll_top_) scroll_top_ = selected_index_;
		if (selected_index_ >= scroll_top_ + list_box_height) scroll_top_ = selected_index_ - list_box_height + 1;
	} else if (focus_idx_ == 2 || focus_idx_ == 3) {
		if (key == ' ' || key == KEY_ENTER || key == 10 || key == 13) {
			return (focus_idx_ == 2) ? dialog_result::confirmed : dialog_result::cancelled;
		}
	}
	
	return dialog_result::pending;
}

std::string file_dialog::get_result() const
{
	return (current_path_ / filename_buffer_).string();
}
