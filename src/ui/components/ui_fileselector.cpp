#include "ui/components/ui_fileselector.h"
#include <ncurses.h>
#include <ctype.h>
#include "fs_utils.h"
#include <sys/stat.h>
#include <algorithm>

// --- ui_fileselector ---

ui_fileselector::ui_fileselector(std::string name, int x, int y, int width, int height, 
					const std::string& initial_path,
					std::function<void(const std::string&)> on_selection_changed,
					std::function<void(const std::string&)> on_submit)
	: ui_element(std::move(name), x, y, width, height), on_selection_changed_(std::move(on_selection_changed)), on_submit_(std::move(on_submit))
{
	try {
		if (!initial_path.empty() && fs::is_directory(initial_path)) {
			current_path_ = fs_utils::safe_absolute(initial_path);
		} else if (!initial_path.empty()) {
			current_path_ = fs_utils::safe_absolute(fs::path(initial_path).parent_path());
			if (current_path_.empty()) current_path_ = fs::current_path();
		} else {
			current_path_ = fs::current_path();
		}
		populate_files();
	} catch (...) {
		current_path_ = fs::current_path();
		populate_files();
	}
}

void ui_fileselector::populate_files()
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
					fe.size = fe.is_dir ? 0 : attr.st_size;
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

			std::sort(directories.begin(), directories.end(), [](const auto &a, const auto &b) { return a.display_name < b.display_name; });
			std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) { return a.display_name < b.display_name; });

			files_.insert(files_.end(), directories.begin(), directories.end());
			files_.insert(files_.end(), files.begin(), files.end());
		}
	} catch (...) {
	}
	
	if (!files_.empty() && on_selection_changed_) {
		on_selection_changed_(files_[selected_index_].display_name);
	}
}

void ui_fileselector::set_current_path(const fs::path& path)
{
	current_path_ = path;
	populate_files();
}

std::optional<file_entry> ui_fileselector::get_selected_entry() const
{
	if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
		return files_[selected_index_];
	}
	return std::nullopt;
}

std::string ui_fileselector::get_autocomplete_suggestion(const std::string& buffer) const
{
	if (buffer.empty()) return "";

	for (const auto &fe : files_) {
		if (!fe.is_dir && fe.display_name == buffer) return "";
		if (fe.is_dir && fe.display_name == buffer + "/") return "";
	}

	std::string suggestion = "";
	std::chrono::system_clock::time_point newest_mtime = std::chrono::system_clock::time_point::min();
	for (const auto &fe : files_) {
		if (!fe.is_dir) {
			if (fe.display_name.starts_with(buffer)) {
				if (fe.mtime >= newest_mtime) {
					newest_mtime = fe.mtime;
					suggestion = fe.display_name;
				}
			}
		}
	}
	return suggestion;
}

void ui_fileselector::draw(int abs_x, int abs_y) const
{
	int list_box_height = height_ - 1; // 7 rows for files, 1 for scrollbar
	int col_width = (width_ - 2) / 2; // e.g. (46 - 2)/2 = 22

	for (int i = 0; i < list_box_height; ++i) {
		move(abs_y + i, abs_x);
		attrset(COLOR_PAIR(17)); // Black on Cyan
		for (int j = 0; j < width_; ++j) addch(' ');
		mvaddstr(abs_y + i, abs_x + col_width, "│");
	}

	for (int i = 0; i < list_box_height; ++i) {
		for (int col = 0; col < 2; ++col) {
			int file_idx = scroll_top_ + i + (col * list_box_height);
			if (file_idx < static_cast<int>(files_.size())) {
				bool is_sel = (file_idx == selected_index_);

				if (is_sel && has_focus_) attrset(COLOR_PAIR(18)); // Bright Yellow on Cyan
				else attrset(COLOR_PAIR(17));

				int draw_col_width = (col == 0) ? col_width : (width_ - col_width - 1);
				std::string name = files_[file_idx].display_name;
				if (name.length() > static_cast<size_t>(draw_col_width)) {
					name = name.substr(0, draw_col_width);
				}
				int draw_x = abs_x + (col * (col_width + 1));
				mvaddstr(abs_y + i, draw_x, name.c_str());
			}
		}
	}

	// Scrollbar
	attrset(COLOR_PAIR(17));
	move(abs_y + list_box_height, abs_x);
	addstr("◄");
	for (int j = 1; j < width_ - 1; ++j) addstr("░");
	addstr("►");
	if (!files_.empty()) {
		int max_scroll = std::max(1, static_cast<int>(files_.size()) - list_box_height * 2);
		int thumb_pos = 1;
		if (max_scroll > 0) {
			thumb_pos = 1 + (scroll_top_ * (width_ - 3)) / max_scroll;
		}
		if (thumb_pos >= width_ - 1) thumb_pos = width_ - 2;
		mvaddstr(abs_y + list_box_height, abs_x + thumb_pos, "■");
	}
	attrset(0);
}

bool ui_fileselector::handle_event(const editor_event &ev, int abs_x, int abs_y)
{
	if (ev.type == event_type::key_press && has_focus_) {
		int files_height = height_ - 1;
		switch (ev.key_code) {
			case KEY_UP:
				if (selected_index_ > 0) selected_index_--;
				break;
			case KEY_DOWN:
				if (selected_index_ < static_cast<int>(files_.size()) - 1) selected_index_++;
				break;
			case KEY_LEFT:
				if (selected_index_ >= files_height) selected_index_ -= files_height;
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
				if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
					const auto &entry = files_[selected_index_];
					if (entry.is_dir) {
						current_path_ = fs::canonical(entry.path);
						populate_files();
					} else {
						if (on_submit_) on_submit_(entry.display_name);
					}
					return true;
				}
				break;
			case '\t':
				if (parent_) {
					ui_element* p = parent_;
					while (p) { if (p->focus_next()) break; p = p->parent(); }
				}
				return true;
			case KEY_BTAB:
				if (parent_) {
					ui_element* p = parent_;
					while (p) { if (p->focus_previous()) break; p = p->parent(); }
				}
				return true;
			default:
				return false;
		}

		while (selected_index_ < scroll_top_) scroll_top_ -= files_height;
		while (selected_index_ >= scroll_top_ + files_height * 2) scroll_top_ += files_height;

		if (selected_index_ >= 0 && selected_index_ < static_cast<int>(files_.size())) {
			if (on_selection_changed_) on_selection_changed_(files_[selected_index_].display_name);
		}
		return true;
	}
	
	if (ev.type == event_type::mouse_click && contains_coordinate(ev.mouse_x, ev.mouse_y, abs_x, abs_y)) {
		if (parent_) parent_->set_focus_by_name(name_);
		int files_height = height_ - 1;
		int rel_y = ev.mouse_y - abs_y;
		int rel_x = ev.mouse_x - abs_x;
		int col_width = (width_ - 2) / 2;
		
		if (rel_y < files_height) {
			int col = (rel_x > col_width) ? 1 : 0;
			int clicked_idx = scroll_top_ + rel_y + (col * files_height);
			if (clicked_idx < static_cast<int>(files_.size())) {
				// If clicking already selected item, act like enter
				if (clicked_idx == selected_index_) {
					const auto &entry = files_[selected_index_];
					if (entry.is_dir) {
						current_path_ = fs::canonical(entry.path);
						populate_files();
					} else {
						if (on_submit_) on_submit_(entry.display_name);
					}
				} else {
					selected_index_ = clicked_idx;
					if (on_selection_changed_) on_selection_changed_(files_[selected_index_].display_name);
				}
				return true;
			}
		}
		return true; // handled click inside us
	}

	return false;
}

// --- ui_file_info_panel ---

ui_file_info_panel::ui_file_info_panel(int x, int y, int width, ui_fileselector* fs_view)
    : ui_element("file_info_panel", x, y, width, 2), fs_view_(fs_view)
{
}

void ui_file_info_panel::draw(int abs_x, int abs_y) const
{
	if (!fs_view_) return;

	attrset(COLOR_PAIR(5));
	for (int i = 0; i < 2; ++i) {
		move(abs_y + i, abs_x);
		for (int j = 0; j < width_; ++j) addch(' ');
	}

	std::string path_str = fs_view_->get_current_path().string();
	if (path_str.length() > static_cast<size_t>(width_ - 2)) {
		path_str = path_str.substr(path_str.length() - (width_ - 2));
	}
	mvaddstr(abs_y, abs_x + 1, path_str.c_str());

	auto sel_entry = fs_view_->get_selected_entry();
	if (sel_entry) {
		std::string info_str = sel_entry->display_name;
		info_str += "  " + std::to_string(sel_entry->size);

		std::time_t t = std::chrono::system_clock::to_time_t(sel_entry->mtime);
		std::tm *tm = std::localtime(&t);
		char time_buf[64];
		std::strftime(time_buf, sizeof(time_buf), "%b %e, %Y %I:%M%p", tm);
		info_str += "  ";
		info_str += time_buf;
		mvaddstr(abs_y + 1, abs_x + 1, info_str.c_str());
	}
	attrset(0);
}

