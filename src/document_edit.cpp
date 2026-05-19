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
#include "fs_utils.h"

namespace fs = std::filesystem;


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


void document::append_line(const std::string &text)
{
	std::unique_lock lock(mutex_);
	
	// If the document is just a single empty line, replace it.
	if (lines_.size() == 1 && lines_[0]->get_text().empty()) {
		lines_[0] = std::make_shared<line>(text);
		mark_line_dirty(lines_[0]);
	} else {
		auto l = std::make_shared<line>(text);
		lines_.push_back(l);
		mark_line_dirty(l);
	}
	
	cursor_y_ = static_cast<int>(lines_.size() - 1);
	cursor_x_ = lines_[cursor_y_]->length_in_chars();
	target_cursor_x_ = cursor_x_;

	set_modified();
	lock.unlock();
	notify_cursor_changed();
}


void document::trim_top_lines(int max_lines)
{
	std::unique_lock lock(mutex_);
	if (static_cast<int>(lines_.size()) <= max_lines || max_lines <= 0) {
		return;
	}

	int lines_to_remove = static_cast<int>(lines_.size()) - max_lines;
	lines_.erase(lines_.begin(), lines_.begin() + lines_to_remove);

	if (cursor_y_ >= lines_to_remove) {
		cursor_y_ -= lines_to_remove;
	} else {
		cursor_y_ = 0;
		cursor_x_ = 0;
	}

	if (selection_start_y_ != -1) {
		selection_start_y_ -= lines_to_remove;
		if (selection_start_y_ < 0) selection_start_y_ = 0;
	}
	if (selection_end_y_ != -1) {
		selection_end_y_ -= lines_to_remove;
		if (selection_end_y_ < 0) selection_end_y_ = 0;
	}

	target_cursor_x_ = cursor_x_;
	
	set_modified();
	lock.unlock();
	notify_cursor_changed();
}


void document::delete_line()
{
	if (is_read_only()) return;
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
