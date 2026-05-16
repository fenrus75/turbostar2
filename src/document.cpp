#include "document.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "event_logger.h"
#include "config_manager.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "clangd_manager.h"

namespace fs = std::filesystem;

document::document(event_queue &global_queue) : global_queue_(global_queue)
{
	lines_.push_back(std::make_shared<line>(""));
	refresh_highlighter();
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	notify_cursor_changed();
}

document::document(event_queue &global_queue, const std::string &filename) : filename_(filename), global_queue_(global_queue)
{
	if (filename.empty() || !load_from_file(filename)) {
		if (lines_.empty())
			lines_.push_back(std::make_shared<line>(""));
	}
	refresh_highlighter();
	highlighter_thread_ = std::thread(&document::highlighter_thread_loop, this);
	notify_cursor_changed();
}

document::~document()
{
	stop_thread_ = true;
	dirty_cv_.notify_all();
	if (highlighter_thread_.joinable()) {
		highlighter_thread_.join();
	}
}

bool document::load_from_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);
	std::ifstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Load failed: Could not open file " + filename);
		return false;
	}

	lines_.clear();
	std::string line_text;
	while (std::getline(file, line_text)) {
		auto l = std::make_shared<line>(line_text);
		lines_.push_back(l);
		mark_line_dirty(l);
	}
	if (lines_.empty()) {
		lines_.push_back(std::make_shared<line>(""));
	}

	filename_ = filename;
	refresh_highlighter();
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;

	undo_stack_.clear();
	redo_stack_.clear();
	current_action_group_.actions.clear();
	edit_group_depth_ = 0;

	event_logger::get_instance().log("Document loaded from: " + filename + " (" +
					 std::to_string(line_count_unlocked()) + " lines)");
	lock.unlock();
	git_manager::get_instance().request_status(filename);
	clangd_manager::get_instance().open_document(filename, get_text_all());
	notify_cursor_changed();
	return true;
}

void document::insert_file(const std::string &filename)
{
	std::ifstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Insert File failed: Could not open file " + filename);
		return;
	}

	std::vector<line> block;
	std::string line_text;
	while (std::getline(file, line_text)) {
		block.emplace_back(line_text);
	}

	if (block.empty())
		return;

	std::unique_lock lock(mutex_);
	begin_edit_group();
	insert_block(block);
	end_edit_group();
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

bool document::save()
{
	std::shared_lock lock(mutex_);
	std::string fname = filename_;
	bool modified = modified_;
	lock.unlock();

	if (fname.empty()) {
		event_logger::get_instance().log("Save failed: No filename specified.");
		return false;
	}

	if (!modified) {
		git_manager::get_instance().request_status(fname);
		return true;
	}

	return save_to_file(fname);
}

bool document::save_to_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);

	if (fs::exists(filename)) {
		try {
			std::error_code ec;
			fs::rename(filename, filename + "~", ec);
			if (ec) {
				event_logger::get_instance().log("Backup rename failed: " + ec.message());
				// Fallback to copy if rename fails (e.g., cross-device, though unlikely here)
				fs::copy_file(filename, filename + "~", fs::copy_options::overwrite_existing);
			}
		} catch (const std::exception &e) {
			event_logger::get_instance().log("Backup failed: " + std::string(e.what()));
		}
	}

	std::ofstream file(filename);
	if (!file.is_open()) {
		event_logger::get_instance().log("Save failed: Could not open file " + filename);
		return false;
	}

	for (int i = 0; i < line_count_unlocked(); ++i) {
		file << lines_[i]->get_text();
		if (i < line_count_unlocked() - 1) {
			file << "\n";
		}
	}

	filename_ = filename;
	refresh_highlighter();
	modified_ = false;
	event_logger::get_instance().log("Document saved to: " + filename);
	lock.unlock();
	git_manager::get_instance().request_status(filename);
	clangd_manager::get_instance().update_document(filename, get_text_all());
	notify_cursor_changed();
	return true;
}

void document::clear()
{
	std::unique_lock lock(mutex_);
	lines_.clear();
	auto l = std::make_shared<line>("");
	lines_.push_back(l);
	mark_line_dirty(l);
	filename_ = "";
	refresh_highlighter();
	modified_ = false;
	cursor_x_ = 0;
	cursor_y_ = 0;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	
	undo_stack_.clear();
	redo_stack_.clear();
	current_action_group_.actions.clear();
	edit_group_depth_ = 0;

	lock.unlock();
	notify_cursor_changed();
}

const std::string &document::get_filename() const
{
	std::shared_lock lock(mutex_);
	return filename_;
}

bool document::has_nondefault_filename() const
{
	std::shared_lock lock(mutex_);
	event_logger::get_instance().log("has_nondefault_filename: current='" + filename_ + "'");
	return !filename_.empty() && filename_ != "unknown.txt";
}

bool document::is_modified() const
{
	std::shared_lock lock(mutex_);
	return modified_;
}

std::string document::get_git_branch() const
{
	std::shared_lock lock(mutex_);
	return git_branch_;
}

void document::set_git_branch(const std::string &branch)
{
	std::unique_lock lock(mutex_);
	git_branch_ = branch;
}

int document::line_count() const
{
	std::shared_lock lock(mutex_);
	return line_count_unlocked();
}

size_t document::get_line_count() const
{
	std::shared_lock lock(mutex_);
	return static_cast<size_t>(line_count_unlocked());
}

int document::line_count_unlocked() const
{
	return static_cast<int>(lines_.size());
}

std::shared_ptr<line> document::get_line(int index) const
{
	std::shared_lock lock(mutex_);
	if (index >= 0 && index < line_count_unlocked())
		return lines_[index];
	return nullptr;
}

int document::get_cursor_x() const
{
	std::shared_lock lock(mutex_);
	return cursor_x_;
}

int document::get_cursor_y() const
{
	std::shared_lock lock(mutex_);
	return cursor_y_;
}

std::string document::get_text_all() const
{
	std::shared_lock lock(mutex_);
	std::string full_text;
	for (size_t i = 0; i < lines_.size(); ++i) {
		full_text += lines_[i]->get_text();
		if (i < lines_.size() - 1) {
			full_text += "\n";
		}
	}
	return full_text;
}

std::string document::get_word_under_cursor() const
{
	std::shared_lock lock(mutex_);
	if (cursor_y_ < 0 || cursor_y_ >= static_cast<int>(lines_.size())) return "";
	std::string text = lines_[cursor_y_]->get_text();
	if (text.empty() || cursor_x_ < 0 || cursor_x_ > static_cast<int>(text.length())) return "";

	int start = cursor_x_;
	while (start > 0) {
		char c = text[start - 1];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			start--;
		} else {
			break;
		}
	}

	int end = cursor_x_;
	while (end < static_cast<int>(text.length())) {
		char c = text[end];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') {
			end++;
		} else {
			break;
		}
	}

	if (start < end) {
		return text.substr(start, end - start);
	}
	return "";
}

void document::move_cursor(int dx, int dy)
{
	std::unique_lock lock(mutex_);

	if (dx != 0) {
		if (dx < 0 && cursor_x_ == 0 && cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		} else if (dx > 0 && cursor_x_ >= lines_[cursor_y_]->length_in_chars() &&
			   cursor_y_ < line_count_unlocked() - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		} else {
			cursor_x_ += dx;
		}
		target_cursor_x_ = cursor_x_;
	}

	if (dy != 0) {
		cursor_y_ += dy;
		cursor_x_ = target_cursor_x_; // Attempt to use target X when moving vertically
	}

	if (cursor_y_ < 0)
		cursor_y_ = 0;
	if (cursor_y_ >= line_count_unlocked())
		cursor_y_ = line_count_unlocked() - 1;

	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ < 0)
		cursor_x_ = 0;
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	lock.unlock();
	notify_cursor_changed();
}

void document::insert_char(const std::string &utf8_char)
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < line_count_unlocked()) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_insert(cursor_y_, cursor_x_, 1);
		lines_[cursor_y_]->insert_at(cursor_x_, utf8_char);
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_++;
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::backspace()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ > 0) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, cursor_x_ - 1, 1);
		lines_[cursor_y_]->remove_at(cursor_x_ - 1);
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_--;
		set_modified();
		end_edit_group();
	} else if (cursor_y_ > 0) {
		// Join with previous line
		int prev_line_idx = cursor_y_ - 1;
		int prev_line_char_len = lines_[prev_line_idx]->length_in_chars();

		begin_edit_group();
		record_action(edit_action::action_type::replace_line, prev_line_idx, lines_[prev_line_idx]);
		record_action(edit_action::action_type::delete_line, cursor_y_, lines_[cursor_y_]);

		adjust_selection_for_join(cursor_y_, cursor_x_);

		lines_[prev_line_idx]->merge(*lines_[cursor_y_]);
		mark_line_dirty(lines_[prev_line_idx]);
		lines_.erase(lines_.begin() + cursor_y_);

		cursor_y_ = prev_line_idx;
		cursor_x_ = prev_line_char_len;
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_char()
{
	std::unique_lock lock(mutex_);
	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ < line_char_len) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, cursor_x_, 1);
		lines_[cursor_y_]->remove_at(cursor_x_);
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
		end_edit_group();
	} else if (cursor_y_ < line_count_unlocked() - 1) {
		// Join next line into this one
		int next_line_idx = cursor_y_ + 1;
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		record_action(edit_action::action_type::delete_line, next_line_idx, lines_[next_line_idx]);
		adjust_selection_for_join(next_line_idx, 0);
		lines_[cursor_y_]->merge(*lines_[next_line_idx]);
		mark_line_dirty(lines_[cursor_y_]);
		lines_.erase(lines_.begin() + next_line_idx);
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_to_eol()
{
	std::unique_lock lock(mutex_);
	int line_char_len = lines_[cursor_y_]->length_in_chars();
	int count = line_char_len - cursor_x_;
	if (count > 0) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, cursor_x_, count);
		for (int i = 0; i < count; ++i) {
			lines_[cursor_y_]->remove_at(cursor_x_);
		}
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_to_bol()
{
	std::unique_lock lock(mutex_);
	int count = cursor_x_;
	if (count > 0) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, 0, count);
		for (int i = 0; i < count; ++i) {
			lines_[cursor_y_]->remove_at(0);
		}
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_ = 0;
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_word_forward()
{
	std::unique_lock lock(mutex_);
	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ >= line_char_len) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_;
	while (i < line_char_len && !is_space_at_unlocked(cursor_y_, i))
		i++;
	while (i < line_char_len && is_space_at_unlocked(cursor_y_, i))
		i++;

	int count = i - cursor_x_;
	if (count > 0) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, cursor_x_, count);
		for (int j = 0; j < count; ++j) {
			lines_[cursor_y_]->remove_at(cursor_x_);
		}
		mark_line_dirty(lines_[cursor_y_]);
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_word_backward()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ == 0) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_ - 1;
	while (i > 0 && is_space_at_unlocked(cursor_y_, i))
		i--;
	while (i > 0 && !is_space_at_unlocked(cursor_y_, i - 1))
		i--;

	int count = cursor_x_ - i;
	if (count > 0) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		adjust_selection_for_delete(cursor_y_, i, count);
		for (int j = 0; j < count; ++j) {
			lines_[cursor_y_]->remove_at(i);
		}
		mark_line_dirty(lines_[cursor_y_]);
		cursor_x_ = i;
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::split_line()
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < line_count_unlocked()) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
		
		adjust_selection_for_split(cursor_y_, cursor_x_);
		auto new_l = std::make_shared<line>("");
		lines_[cursor_y_]->split_at(cursor_x_, *new_l);
		mark_line_dirty(lines_[cursor_y_]);
		mark_line_dirty(new_l);
		lines_.insert(lines_.begin() + cursor_y_ + 1, new_l);
		
		record_action(edit_action::action_type::insert_line, cursor_y_ + 1, nullptr);
		
		cursor_y_++;
		cursor_x_ = 0;
		set_modified();
		end_edit_group();
	}
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_bol()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_eol()
{
	std::unique_lock lock(mutex_);
	if (cursor_y_ >= 0 && cursor_y_ < line_count_unlocked()) {
		cursor_x_ = lines_[cursor_y_]->length_in_chars();
	}
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_top()
{
	std::unique_lock lock(mutex_);
	cursor_x_ = 0;
	cursor_y_ = 0;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_to_bottom()
{
	std::unique_lock lock(mutex_);
	cursor_y_ = line_count_unlocked() - 1;
	cursor_x_ = lines_[cursor_y_]->length_in_chars();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_page_up(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ -= page_height;
	cursor_y_ = std::max(0, cursor_y_);
	cursor_x_ = target_cursor_x_;

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_page_down(int page_height)
{
	std::unique_lock lock(mutex_);
	cursor_y_ += page_height;
	if (cursor_y_ >= line_count_unlocked()) {
		cursor_y_ = line_count_unlocked() - 1;
	}
	cursor_x_ = target_cursor_x_;

	int line_char_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_char_len)
		cursor_x_ = line_char_len;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_next_word()
{
	std::unique_lock lock(mutex_);
	std::string text = lines_[cursor_y_]->get_text();
	int line_char_len = lines_[cursor_y_]->length_in_chars();

	if (cursor_x_ >= line_char_len) {
		if (cursor_y_ < line_count_unlocked() - 1) {
			cursor_y_++;
			cursor_x_ = 0;
		}
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_;
	while (i < line_char_len && !is_space_at_unlocked(cursor_y_, i))
		i++;
	while (i < line_char_len && is_space_at_unlocked(cursor_y_, i))
		i++;

	if (i >= line_char_len && cursor_y_ < line_count_unlocked() - 1) {
		cursor_y_++;
		cursor_x_ = 0;
	} else {
		cursor_x_ = i;
	}

	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::move_prev_word()
{
	std::unique_lock lock(mutex_);
	if (cursor_x_ == 0) {
		if (cursor_y_ > 0) {
			cursor_y_--;
			cursor_x_ = lines_[cursor_y_]->length_in_chars();
		}
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int i = cursor_x_ - 1;
	while (i > 0 && is_space_at_unlocked(cursor_y_, i))
		i--;
	while (i > 0 && !is_space_at_unlocked(cursor_y_, i - 1))
		i--;

	cursor_x_ = i;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::delete_line()
{
	std::unique_lock lock(mutex_);
	if (line_count_unlocked() <= 1) {
		begin_edit_group();
		record_action(edit_action::action_type::replace_line, 0, lines_[0]);
		lines_[0]->set_text("");
		mark_line_dirty(lines_[0]);
		selection_start_x_ = selection_start_y_ = -1;
		selection_end_x_ = selection_end_y_ = -1;
		cursor_x_ = 0;
		cursor_y_ = 0;
		set_modified();
		end_edit_group();
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	begin_edit_group();
	record_action(edit_action::action_type::delete_line, cursor_y_, lines_[cursor_y_]);
	adjust_selection_for_line_delete(cursor_y_);
	lines_.erase(lines_.begin() + cursor_y_);
	if (cursor_y_ >= line_count_unlocked()) {
		cursor_y_ = line_count_unlocked() - 1;
	}

	cursor_x_ = 0;
	set_modified();
	end_edit_group();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::format_range(int start_y, int end_y)
{
	if (start_y < 0 || end_y >= line_count_unlocked() || start_y > end_y)
		return;

	std::string temp_path = "format_tmp.cpp";
	std::ofstream tmp_file(temp_path);
	if (!tmp_file.is_open()) {
		event_logger::get_instance().log("Format failed: Could not create temp file.");
		return;
	}

	{
		std::shared_lock lock(mutex_);
		for (int i = 0; i < line_count_unlocked(); ++i) {
			if (i == start_y) {
				tmp_file << "// TS_FORMAT_START\n";
			}
			tmp_file << lines_[i]->get_text() << "\n";
			if (i == end_y) {
				tmp_file << "// TS_FORMAT_END\n";
			}
		}
	}
	tmp_file.close();

	// Determine style
	std::string style = config_manager::get_instance().get_clang_format_style();
	
	// Check if a .clang-format file exists in the project. 
	// Policy: If a .clang-format file exists in the file's directory or any parent, it always wins.
	bool force_file = false;
	fs::path search_path;
	if (!filename_.empty() && filename_ != "unknown.txt") {
		search_path = fs::absolute(filename_).parent_path();
	} else {
		search_path = fs::current_path();
	}

	while (true) {
		if (fs::exists(search_path / ".clang-format")) {
			force_file = true;
			break;
		}
		if (!search_path.has_parent_path() || search_path == search_path.parent_path())
			break;
		search_path = search_path.parent_path();
	}

	std::string style_arg = "--style=" + (force_file ? "file" : style);

	// Run clang-format
	std::string cmd = "clang-format " + style_arg + " -i " + temp_path;
	if (std::system(cmd.c_str()) != 0) {
		event_logger::get_instance().log("Format failed: clang-format returned error.");
		return;
	}

	// Read back and extract
	std::ifstream result_file(temp_path);
	if (!result_file.is_open()) {
		return;
	}

	std::vector<line> formatted_block;
	std::string line_text;
	bool inside_markers = false;
	while (std::getline(result_file, line_text)) {
		if (line_text.find("// TS_FORMAT_START") != std::string::npos) {
			inside_markers = true;
			continue;
		}
		if (line_text.find("// TS_FORMAT_END") != std::string::npos) {
			inside_markers = false;
			break;
		}
		if (inside_markers) {
			formatted_block.emplace_back(line_text);
		}
	}
	result_file.close();
	fs::remove(temp_path);

	if (formatted_block.empty()) {
		event_logger::get_instance().log("Format failed: Could not find markers in output.");
		return;
	}

	// Replace the range
	std::unique_lock lock(mutex_);
	begin_edit_group();
	
	// 1. Insert new lines
	for (size_t i = 0; i < formatted_block.size(); ++i) {
		auto nl = std::make_shared<line>(formatted_block[i]);
		lines_.insert(lines_.begin() + start_y + i, nl);
		record_action(edit_action::action_type::insert_line, start_y + static_cast<int>(i), nullptr);
		mark_line_dirty(nl);
	}
	
	// 2. Delete old lines (now shifted forward by formatted_block.size())
	int old_start = start_y + static_cast<int>(formatted_block.size());
	int num_to_delete = end_y - start_y + 1;
	for (int i = 0; i < num_to_delete; ++i) {
		record_action(edit_action::action_type::delete_line, old_start, lines_[old_start]);
		lines_.erase(lines_.begin() + old_start);
	}
	
	cursor_y_ = start_y;
	cursor_x_ = 0;
	
	end_edit_group();
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::format_paragraph()
{
	std::shared_lock lock(mutex_);
	if (lines_.empty())
		return;

	auto is_empty = [&](int y) {
		std::string t = lines_[y]->get_text();
		return std::all_of(t.begin(), t.end(), [](unsigned char ch) { return std::isspace(ch); });
	};

	int sy = cursor_y_;
	int ey = cursor_y_;

	// If we are on an empty line, do nothing or just that line (which is a no-op)
	if (is_empty(cursor_y_)) {
		lock.unlock();
		return;
	}

	while (sy > 0 && !is_empty(sy - 1)) {
		sy--;
	}
	while (ey < line_count_unlocked() - 1 && !is_empty(ey + 1)) {
		ey++;
	}
	lock.unlock();

	format_range(sy, ey);
}

void document::set_selection_start()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = cursor_x_;
	selection_start_y_ = cursor_y_;
	lock.unlock();
	notify_cursor_changed();
}

void document::set_selection_end()
{
	std::unique_lock lock(mutex_);
	selection_end_x_ = cursor_x_;
	selection_end_y_ = cursor_y_;
	lock.unlock();
	notify_cursor_changed();
}

void document::set_selection(int start_y, int start_x, int end_y, int end_x)
{
	std::unique_lock lock(mutex_);
	selection_start_y_ = start_y;
	selection_start_x_ = start_x;
	selection_end_y_ = end_y;
	selection_end_x_ = end_x;
	lock.unlock();
	notify_cursor_changed();
}

void document::clear_selection()
{
	std::unique_lock lock(mutex_);
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	lock.unlock();
	notify_cursor_changed();
}

std::vector<line> document::get_selection_block() const
{
	if (selection_start_y_ == -1 || selection_end_y_ == -1)
		return {};

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_;
		sy = selection_start_y_;
		ex = selection_end_x_;
		ey = selection_end_y_;
	} else {
		sx = selection_end_x_;
		sy = selection_end_y_;
		ex = selection_start_x_;
		ey = selection_start_y_;
	}

	std::vector<line> block;
	if (sy == ey) {
		line l(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx),
						     lines_[sy]->char_to_byte_offset(ex) - lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l));
	} else {
		line l1(lines_[sy]->get_text().substr(lines_[sy]->char_to_byte_offset(sx)));
		block.push_back(std::move(l1));
		for (int i = sy + 1; i < ey; ++i)
			block.push_back(*lines_[i]);
		line ln(lines_[ey]->get_text().substr(0, lines_[ey]->char_to_byte_offset(ex)));
		block.push_back(std::move(ln));
	}
	return block;
}

void document::delete_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	int sx, sy, ex, ey;
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		sx = selection_start_x_;
		sy = selection_start_y_;
		ex = selection_end_x_;
		ey = selection_end_y_;
	} else {
		sx = selection_end_x_;
		sy = selection_end_y_;
		ex = selection_start_x_;
		ey = selection_start_y_;
	}

	begin_edit_group();

	if (sy == ey) {
		record_action(edit_action::action_type::replace_line, sy, lines_[sy]);
		for (int i = 0; i < (ex - sx); ++i)
			lines_[sy]->remove_at(sx);
		mark_line_dirty(lines_[sy]);
	} else {
		record_action(edit_action::action_type::replace_line, sy, lines_[sy]);
		// Record deletions in reverse order so undo (which reverses) inserts them correctly
		for (int i = ey; i > sy; --i) {
			record_action(edit_action::action_type::delete_line, i, lines_[i]);
		}
		
		line tail_line("");
		lines_[ey]->split_at(ex, tail_line);
		line throwaway("");
		lines_[sy]->split_at(sx, throwaway);
		lines_[sy]->merge(tail_line);
		mark_line_dirty(lines_[sy]);
		lines_.erase(lines_.begin() + sy + 1, lines_.begin() + ey + 1);
	}

	end_edit_group();

	cursor_x_ = sx;
	cursor_y_ = sy;
	selection_start_x_ = selection_start_y_ = -1;
	selection_end_x_ = selection_end_y_ = -1;
	set_modified();
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::copy_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;
	
	begin_edit_group();
	insert_block(block);
	end_edit_group();

	selection_start_x_ = tx;
	selection_start_y_ = ty;
	selection_end_y_ = ty + block.size() - 1;
	if (block.size() == 1)
		selection_end_x_ = tx + block[0].length_in_chars();
	else
		selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	notify_cursor_changed();
}

void document::move_selection()
{
	std::unique_lock lock(mutex_);
	if (selection_start_y_ == -1 || selection_end_y_ == -1) {
		lock.unlock();
		notify_cursor_changed();
		return;
	}

	std::vector<line> block = get_selection_block();
	int tx = cursor_x_;
	int ty = cursor_y_;

	begin_edit_group();

	lock.unlock();
	delete_selection();
	lock.lock();

	cursor_x_ = tx;
	cursor_y_ = ty;
	if (cursor_y_ >= line_count_unlocked())
		cursor_y_ = line_count_unlocked() - 1;
	int line_len = lines_[cursor_y_]->length_in_chars();
	if (cursor_x_ > line_len)
		cursor_x_ = line_len;

	int fx = cursor_x_;
	int fy = cursor_y_;
	insert_block(block);

	end_edit_group();

	selection_start_x_ = fx;
	selection_start_y_ = fy;
	selection_end_y_ = fy + block.size() - 1;
	if (block.size() == 1)
		selection_end_x_ = fx + block[0].length_in_chars();
	else
		selection_end_x_ = block.back().length_in_chars();

	set_modified();
	lock.unlock();
	notify_cursor_changed();
}

void document::insert_block(const std::vector<line> &block)
{
	if (block.empty())
		return;
	
	begin_edit_group();
	record_action(edit_action::action_type::replace_line, cursor_y_, lines_[cursor_y_]);
	
	line tail("");
	lines_[cursor_y_]->split_at(cursor_x_, tail);
	lines_[cursor_y_]->merge(block[0]);
	mark_line_dirty(lines_[cursor_y_]);
	if (block.size() > 1) {
		for (size_t i = 1; i < block.size(); ++i) {
			auto nl = std::make_shared<line>(block[i]);
			lines_.insert(lines_.begin() + cursor_y_ + i, nl);
			record_action(edit_action::action_type::insert_line, cursor_y_ + i, nullptr);
			mark_line_dirty(nl);
		}
		
		record_action(edit_action::action_type::replace_line, cursor_y_ + block.size() - 1, lines_[cursor_y_ + block.size() - 1]);
		lines_[cursor_y_ + block.size() - 1]->merge(tail);
		mark_line_dirty(lines_[cursor_y_ + block.size() - 1]);
	} else {
		lines_[cursor_y_]->merge(tail);
		mark_line_dirty(lines_[cursor_y_]);
	}
	cursor_y_ += block.size() - 1;
	if (block.size() == 1)
		cursor_x_ += block[0].length_in_chars();
	else
		cursor_x_ = block.back().length_in_chars();
		
	end_edit_group();
}

bool document::has_selection() const
{
	std::shared_lock lock(mutex_);
	return selection_start_y_ != -1 && selection_end_y_ != -1;
}

void document::get_selection_range(int &start_x, int &start_y, int &end_x, int &end_y) const
{
	std::shared_lock lock(mutex_);
	if (selection_start_y_ < selection_end_y_ || (selection_start_y_ == selection_end_y_ && selection_start_x_ <= selection_end_x_)) {
		start_x = selection_start_x_;
		start_y = selection_start_y_;
		end_x = selection_end_x_;
		end_y = selection_end_y_;
	} else {
		start_x = selection_end_x_;
		start_y = selection_end_y_;
		end_x = selection_start_x_;
		end_y = selection_start_y_;
	}
}
void document::notify_cursor_changed() const
{
	std::string word = get_word_under_cursor();
	if (!filename_.empty() && filename_ != "unknown.txt" && word != last_hover_word_) {
		last_hover_word_ = word;
		if (!word.empty()) {
			clangd_manager::get_instance().request_hover(filename_, cursor_y_, cursor_x_);
			clangd_manager::get_instance().request_document_highlight(filename_, cursor_y_, cursor_x_);
		} else {
			// Clear highlights if we moved off a word
			std::unique_lock lock2(mutex_);
			lsp_highlights_.clear();
			lock2.unlock();
			
			editor_event ev;
			ev.type = event_type::redraw;
			global_queue_.push(ev);
		}
	}

	std::shared_lock lock(mutex_);
	int cur_disp_x = lines_[cursor_y_]->char_to_display_col(cursor_x_);
	std::string msg = "State: C=" + std::to_string(cursor_y_ + 1) + ":" + std::to_string(cur_disp_x + 1);

	if (selection_start_y_ != -1) {
		int sel_start_disp_x = lines_[selection_start_y_]->char_to_display_col(selection_start_x_);
		msg += " S=" + std::to_string(selection_start_y_ + 1) + ":" + std::to_string(sel_start_disp_x + 1);
	} else {
		msg += " S=none";
	}

	if (selection_end_y_ != -1) {
		int sel_end_disp_x = lines_[selection_end_y_]->char_to_display_col(selection_end_x_);
		msg += " E=" + std::to_string(selection_end_y_ + 1) + ":" + std::to_string(sel_end_disp_x + 1);
	} else {
		msg += " E=none";
	}

	event_logger::get_instance().log(msg);
}
void document::set_modified()
{
	modified_ = true;
}

void document::adjust_selection_for_insert(int y, int x, int count)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			if (sx >= x)
				sx += count;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_delete(int y, int x, int count)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			if (sx > x + count)
				sx -= count;
			else if (sx > x)
				sx = x;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_split(int y, int x)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y && sx >= x) {
			sy++;
			sx -= x;
		} else if (sy > y) {
			sy++;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_join(int y, int x)
{
	(void)x;
	int prev_line_char_len = lines_[y - 1]->length_in_chars();
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			sy--;
			sx += prev_line_char_len;
		} else if (sy > y) {
			sy--;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

void document::adjust_selection_for_line_delete(int y)
{
	auto adjust = [&](int &sx, int &sy) {
		if (sy == y) {
			sx = 0;
			if (y >= line_count_unlocked() - 1) {
				sy = y - 1;
				sx = lines_[sy]->length_in_chars();
			}
		} else if (sy > y) {
			sy--;
		}
	};
	adjust(selection_start_x_, selection_start_y_);
	adjust(selection_end_x_, selection_end_y_);
}

bool document::find_next(const search_params &params, bool is_repeat)
{
	if (params.query.empty())
		return false;

	std::unique_lock lock(mutex_);
	int start_y = cursor_y_;
	int start_x = cursor_x_;

	int scope_sy = 0, scope_sx = 0, scope_ey = line_count_unlocked() - 1, scope_ex = lines_.back()->length_in_chars();
	if (params.selected_text_only && selection_start_y_ != -1) {
		get_selection_range(scope_sx, scope_sy, scope_ex, scope_ey);
	}

	if (!params.from_cursor) {
		if (params.backward) {
			start_y = scope_ey;
			start_x = scope_ex;
		} else {
			start_y = scope_sy;
			start_x = scope_sx;
		}
	} else if (is_repeat) {
		// Step over current char
		if (params.backward) {
			if (start_x > 0)
				start_x--;
			else if (start_y > scope_sy) {
				start_y--;
				start_x = lines_[start_y]->length_in_chars();
			} else
				return false;
		} else {
			if (start_x < lines_[start_y]->length_in_chars())
				start_x++;
			else if (start_y < scope_ey) {
				start_y++;
				start_x = 0;
			} else
				return false;
		}
	}

	auto check_line = [&](int y, int x_limit) -> int {
		std::string line_text = lines_[y]->get_text();
		std::string original_line_text = line_text;

		std::regex_constants::syntax_option_type flags = std::regex::ECMAScript;
		if (params.ignore_case)
			flags |= std::regex::icase;

		std::string pattern = params.query;
		if (params.whole_words && !params.regex) {
			pattern = "\\b" + pattern + "\\b";
		}

		try {
			std::regex re(pattern, flags);
			auto words_begin = std::sregex_iterator(line_text.begin(), line_text.end(), re);
			auto words_end = std::sregex_iterator();

			int best_found_char_idx = -1;
			size_t byte_limit = lines_[y]->char_to_byte_offset(x_limit);

			size_t line_scope_start_byte = 0;
			size_t line_scope_end_byte = line_text.length();
			if (params.selected_text_only) {
				if (y == scope_sy)
					line_scope_start_byte = lines_[y]->char_to_byte_offset(scope_sx);
				if (y == scope_ey)
					line_scope_end_byte = lines_[y]->char_to_byte_offset(scope_ex);
			}

			for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
				std::smatch match = *i;
				size_t byte_pos = match.position();

				if (params.backward) {
					if (byte_pos >= line_scope_start_byte && byte_pos <= byte_limit) {
						int found_char_idx = 0;
						size_t current_byte = 0;
						while (current_byte < byte_pos && current_byte < original_line_text.length()) {
							unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
							if (c < 0x80)
								current_byte += 1;
							else if ((c & 0xE0) == 0xC0)
								current_byte += 2;
							else if ((c & 0xE0) == 0xE0)
								current_byte += 3;
							else if ((c & 0xF0) == 0xF0)
								current_byte += 4;
							else
								current_byte += 1;
							found_char_idx++;
						}
						best_found_char_idx = found_char_idx;
					}
				} else {
					if (byte_pos >= byte_limit && byte_pos < line_scope_end_byte) {
						int found_char_idx = 0;
						size_t current_byte = 0;
						while (current_byte < byte_pos && current_byte < original_line_text.length()) {
							unsigned char c = static_cast<unsigned char>(original_line_text[current_byte]);
							if (c < 0x80)
								current_byte += 1;
							else if ((c & 0xE0) == 0xC0)
								current_byte += 2;
							else if ((c & 0xE0) == 0xE0)
								current_byte += 3;
							else if ((c & 0xF0) == 0xF0)
								current_byte += 4;
							else
								current_byte += 1;
							found_char_idx++;
						}
						return found_char_idx;
					}
				}
			}
			return best_found_char_idx;
		} catch (...) {
			return -1;
		}
	};

	if (params.backward) {
		for (int y = start_y; y >= scope_sy; --y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = lines_[y]->length_in_chars();
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				notify_cursor_changed();
				return true;
			}
		}
	} else {
		for (int y = start_y; y <= scope_ey; ++y) {
			int x_lim;
			if (y == start_y) {
				x_lim = start_x;
			} else {
				x_lim = 0;
			}
			int found_x = check_line(y, x_lim);
			if (found_x != -1) {
				cursor_y_ = y;
				cursor_x_ = found_x;
				lock.unlock();
				notify_cursor_changed();
				return true;
			}
		}
	}

	lock.unlock();
	notify_cursor_changed();
	return false;
}

void document::mark_line_dirty(std::shared_ptr<line> l)
{
	std::lock_guard lock(dirty_mutex_);
	dirty_lines_.push(l);
	dirty_cv_.notify_one();
}

void document::highlighter_thread_loop()
{
	while (!stop_thread_) {
		std::shared_ptr<line> l;
		{
			std::unique_lock lock(dirty_mutex_);
			dirty_cv_.wait(lock, [&] { return !dirty_lines_.empty() || stop_thread_; });
			if (stop_thread_)
				break;
			l = dirty_lines_.front();
			dirty_lines_.pop();
		}
		if (l) {
			process_line_highlight(l);

			// If no more lines in queue, request a redraw
			{
				std::lock_guard lock(dirty_mutex_);
				if (dirty_lines_.empty()) {
					editor_event ev;
					ev.type = event_type::redraw;
					global_queue_.push(ev);
				}
			}
		}
	}
}

void document::refresh_highlighter()
{
	active_highlighter_ = highlighter_registry::get_instance().get_highlighter_for_file(filename_);
}

void document::process_line_highlight(std::shared_ptr<line> l)
{
	if (active_highlighter_) {
		active_highlighter_->highlight(l);
	}
}

bool document::is_space_at(int y, int x) const
{
	std::shared_lock lock(mutex_);
	return is_space_at_unlocked(y, x);
}

bool document::is_space_at_unlocked(int y, int x) const
{
	std::string text = lines_[y]->get_text();
	size_t offset = lines_[y]->char_to_byte_offset(x);
	if (offset < text.length()) {
		return std::isspace(static_cast<unsigned char>(text[offset]));
	}
	return false;
}

void document::begin_edit_group()
{
	if (!is_recording_actions_)
		return;
	if (edit_group_depth_ == 0) {
		current_action_group_.actions.clear();
		current_action_group_.cursor_y_before = cursor_y_;
		current_action_group_.cursor_x_before = cursor_x_;
	}
	edit_group_depth_++;
}

void document::end_edit_group()
{
	if (!is_recording_actions_)
		return;
	edit_group_depth_--;
	if (edit_group_depth_ == 0 && !current_action_group_.empty()) {
		current_action_group_.cursor_y_after = cursor_y_;
		current_action_group_.cursor_x_after = cursor_x_;
		undo_stack_.push_back(current_action_group_);
		if (undo_stack_.size() > max_undo_steps_) {
			undo_stack_.pop_front();
		}
		redo_stack_.clear();
		current_action_group_.actions.clear();
	}
}

void document::record_action(edit_action::action_type type, int y, std::shared_ptr<line> saved_line)
{
	if (!is_recording_actions_)
		return;

	edit_action act;
	act.type = type;
	act.y = y;
	if (saved_line) {
		act.saved_line = std::make_shared<line>(*saved_line);
	}

	current_action_group_.actions.push_back(act);
}

std::optional<std::pair<int, int>> document::find_matching_bracket(int start_y, int start_x) const
{
	std::shared_lock lock(mutex_);
	if (start_y < 0 || start_y >= line_count_unlocked())
		return std::nullopt;

	std::string text = lines_[start_y]->get_text();
	if (start_x < 0 || start_x >= static_cast<int>(text.length()))
		return std::nullopt;

	char start_char = text[start_x];
	char target_char = 0;
	bool forward = true;

	if (start_char == '(') { target_char = ')'; forward = true; }
	else if (start_char == '[') { target_char = ']'; forward = true; }
	else if (start_char == '{') { target_char = '}'; forward = true; }
	else if (start_char == ')') { target_char = '('; forward = false; }
	else if (start_char == ']') { target_char = '['; forward = false; }
	else if (start_char == '}') { target_char = '{'; forward = false; }
	else return std::nullopt;

	int depth = 0;
	if (forward) {
		for (int y = start_y; y < line_count_unlocked(); ++y) {
			std::string l_text = lines_[y]->get_text();
			for (int x = (y == start_y ? start_x : 0); x < static_cast<int>(l_text.length()); ++x) {
				if (l_text[x] == start_char) depth++;
				else if (l_text[x] == target_char) depth--;
				
				if (depth == 0) return std::make_pair(y, x);
			}
		}
	} else {
		for (int y = start_y; y >= 0; --y) {
			std::string l_text = lines_[y]->get_text();
			for (int x = (y == start_y ? start_x : static_cast<int>(l_text.length()) - 1); x >= 0; --x) {
				if (l_text[x] == start_char) depth++;
				else if (l_text[x] == target_char) depth--;
				
				if (depth == 0) return std::make_pair(y, x);
			}
		}
	}

	return std::nullopt;
}

void document::select_enclosing_scope()
{
	std::shared_lock lock(mutex_);
	int sy = cursor_y_;
	int sx = cursor_x_;

	while (sy >= 0) {
		std::string text = lines_[sy]->get_text();
		int start_x = (sy == cursor_y_) ? std::min(sx, static_cast<int>(text.length()) - 1) : static_cast<int>(text.length()) - 1;
		
		for (int x = start_x; x >= 0; --x) {
			if (text[x] == '{') {
				// Potential start. Find match.
				lock.unlock();
				auto match = find_matching_bracket(sy, x);
				lock.lock();

				if (match) {
					// Check if cursor is inside (or on the boundaries)
					bool is_inside = false;
					if (match->first > cursor_y_ || (match->first == cursor_y_ && match->second >= cursor_x_)) {
						is_inside = true;
					}

					if (is_inside) {
						lock.unlock();
						std::unique_lock ulock(mutex_);
						selection_start_y_ = sy;
						selection_start_x_ = x;
						selection_end_y_ = match->first;
						selection_end_x_ = match->second + 1; // Include the closing brace
						ulock.unlock();
						notify_cursor_changed();
						return;
					}
				}
			}
		}
		sy--;
	}
}

void document::undo()
{
	std::unique_lock lock(mutex_);
	if (undo_stack_.empty())
		return;

	is_recording_actions_ = false;
	action_group group = undo_stack_.back();
	undo_stack_.pop_back();

	action_group inverse_group;
	inverse_group.cursor_y_before = group.cursor_y_after;
	inverse_group.cursor_x_before = group.cursor_x_after;
	inverse_group.cursor_y_after = group.cursor_y_before;
	inverse_group.cursor_x_after = group.cursor_x_before;

	// Process actions in reverse order
	for (auto it = group.actions.rbegin(); it != group.actions.rend(); ++it) {
		const auto &act = *it;
		edit_action inverse_act;
		inverse_act.y = act.y;

		if (act.type == edit_action::action_type::replace_line) {
			inverse_act.type = edit_action::action_type::replace_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_[act.y] = std::make_shared<line>(*act.saved_line);
			mark_line_dirty(lines_[act.y]);
		} else if (act.type == edit_action::action_type::insert_line) {
			inverse_act.type = edit_action::action_type::delete_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_.erase(lines_.begin() + act.y);
			adjust_selection_for_line_delete(act.y);
		} else if (act.type == edit_action::action_type::delete_line) {
			inverse_act.type = edit_action::action_type::insert_line;
			lines_.insert(lines_.begin() + act.y, std::make_shared<line>(*act.saved_line));
			mark_line_dirty(lines_[act.y]);
		}

		inverse_group.actions.push_back(inverse_act);
	}

	cursor_y_ = group.cursor_y_before;
	cursor_x_ = group.cursor_x_before;

	std::reverse(inverse_group.actions.begin(), inverse_group.actions.end());
	redo_stack_.push_back(inverse_group);
	
	set_modified();
	is_recording_actions_ = true;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}

void document::redo()
{
	std::unique_lock lock(mutex_);
	if (redo_stack_.empty())
		return;

	is_recording_actions_ = false;
	action_group group = redo_stack_.back();
	redo_stack_.pop_back();

	action_group inverse_group;
	inverse_group.cursor_y_before = group.cursor_y_after;
	inverse_group.cursor_x_before = group.cursor_x_after;
	inverse_group.cursor_y_after = group.cursor_y_before;
	inverse_group.cursor_x_after = group.cursor_x_before;

	for (auto it = group.actions.rbegin(); it != group.actions.rend(); ++it) {
		const auto &act = *it;
		edit_action inverse_act;
		inverse_act.y = act.y;

		if (act.type == edit_action::action_type::replace_line) {
			inverse_act.type = edit_action::action_type::replace_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_[act.y] = std::make_shared<line>(*act.saved_line);
			mark_line_dirty(lines_[act.y]);
		} else if (act.type == edit_action::action_type::insert_line) {
			inverse_act.type = edit_action::action_type::delete_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_.erase(lines_.begin() + act.y);
			adjust_selection_for_line_delete(act.y);
		} else if (act.type == edit_action::action_type::delete_line) {
			inverse_act.type = edit_action::action_type::insert_line;
			lines_.insert(lines_.begin() + act.y, std::make_shared<line>(*act.saved_line));
			mark_line_dirty(lines_[act.y]);
		}

		inverse_group.actions.push_back(inverse_act);
	}

	cursor_y_ = group.cursor_y_before;
	cursor_x_ = group.cursor_x_before;

	std::reverse(inverse_group.actions.begin(), inverse_group.actions.end());
	undo_stack_.push_back(inverse_group);

	set_modified();
	is_recording_actions_ = true;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}
